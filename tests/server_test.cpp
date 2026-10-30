#include "memkv/net/server.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "memkv/common/clock.hpp"

namespace memkv::net {
namespace {

/// Blocking test client speaking raw bytes, so tests see exactly what goes over the wire.
class Client {
public:
    explicit Client(std::uint16_t port) : fd_(::socket(AF_INET, SOCK_STREAM, 0)) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        connected_ =
            ::connect(fd_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    }

    [[nodiscard]] bool connected() const { return connected_; }

    void send(const std::string& data) {
        ASSERT_EQ(::send(fd_.get(), data.data(), data.size(), 0),
                  static_cast<ssize_t>(data.size()));
    }

    /// Reads until exactly `size` bytes have arrived.
    std::string receive(std::size_t size) {
        std::string data(size, '\0');
        std::size_t received = 0;
        while (received < size) {
            const ssize_t n = ::recv(fd_.get(), data.data() + received, size - received, 0);
            if (n <= 0) {
                break;
            }
            received += static_cast<std::size_t>(n);
        }
        data.resize(received);
        return data;
    }

    /// True once the server has closed the connection.
    bool closed_by_peer() {
        char byte = 0;
        return ::recv(fd_.get(), &byte, 1, 0) == 0;
    }

    void shutdown_writes() { ::shutdown(fd_.get(), SHUT_WR); }

private:
    UniqueFd fd_;
    bool connected_ = false;
};

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<Server>(ServerConfig{.host = "127.0.0.1", .port = 0}, db_);
        thread_ = std::thread([this] { server_->run(); });
    }

    void TearDown() override {
        server_->stop();
        thread_.join();
    }

    std::string round_trip(Client& client, const std::string& request, std::size_t reply_size) {
        client.send(request);
        return client.receive(reply_size);
    }

    SteadyClock clock_;
    storage::Database db_{clock_};
    std::unique_ptr<Server> server_;
    std::thread thread_;
};

TEST_F(ServerTest, ServesRespAndInlineCommands) {
    Client client(server_->port());
    ASSERT_TRUE(client.connected());
    EXPECT_EQ(round_trip(client, "*1\r\n$4\r\nPING\r\n", 7), "+PONG\r\n");
    EXPECT_EQ(round_trip(client, "SET greeting hello\r\n", 5), "+OK\r\n");
    EXPECT_EQ(round_trip(client, "*2\r\n$3\r\nGET\r\n$8\r\ngreeting\r\n", 11), "$5\r\nhello\r\n");
}

TEST_F(ServerTest, AnswersPipelinedRequestsInOrder) {
    Client client(server_->port());
    std::string batch;
    std::string expected;
    for (int i = 0; i < 1000; ++i) {
        const std::string value = std::to_string(i);
        batch += "SET k" + value + " " + value + "\r\nGET k" + value + "\r\n";
        expected += "+OK\r\n$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
    }
    EXPECT_EQ(round_trip(client, batch, expected.size()), expected);
}

TEST_F(ServerTest, HandlesLargeValuesAcrossManyPackets) {
    Client client(server_->port());
    const std::string value(4 * 1024 * 1024, 'x');
    client.send("*3\r\n$3\r\nSET\r\n$3\r\nbig\r\n$" + std::to_string(value.size()) + "\r\n" +
                value + "\r\n");
    EXPECT_EQ(client.receive(5), "+OK\r\n");
    const std::string header = "$" + std::to_string(value.size()) + "\r\n";
    EXPECT_EQ(round_trip(client, "GET big\r\n", header.size() + value.size() + 2),
              header + value + "\r\n");
}

TEST_F(ServerTest, ServesMultipleClientsIndependently) {
    Client first(server_->port());
    Client second(server_->port());
    EXPECT_EQ(round_trip(first, "SET shared 1\r\n", 5), "+OK\r\n");
    EXPECT_EQ(round_trip(second, "GET shared\r\n", 7), "$1\r\n1\r\n");
}

TEST_F(ServerTest, QuitRepliesThenCloses) {
    Client client(server_->port());
    EXPECT_EQ(round_trip(client, "QUIT\r\n", 5), "+OK\r\n");
    EXPECT_TRUE(client.closed_by_peer());
}

TEST_F(ServerTest, ProtocolErrorRepliesThenCloses) {
    Client client(server_->port());
    const std::string reply = round_trip(client, "*1\r\n:oops\r\n", 64);
    EXPECT_TRUE(reply.starts_with("-ERR Protocol error")) << reply;
    EXPECT_TRUE(client.closed_by_peer());
}

TEST_F(ServerTest, RepliesToRequestSentBeforeHalfClose) {
    Client client(server_->port());
    client.send("PING\r\n");
    client.shutdown_writes();
    EXPECT_EQ(client.receive(7), "+PONG\r\n");
}

TEST_F(ServerTest, ActiveExpirationRunsInBackground) {
    Client client(server_->port());
    EXPECT_EQ(round_trip(client, "SET temp v PX 50\r\n", 5), "+OK\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    // DBSIZE does not touch individual keys, so only the cron can have removed it.
    EXPECT_EQ(round_trip(client, "DBSIZE\r\n", 4), ":0\r\n");
}

}  // namespace
}  // namespace memkv::net
