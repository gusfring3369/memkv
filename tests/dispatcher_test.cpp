#include "memkv/command/dispatcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "support/manual_clock.hpp"

namespace memkv::command {
namespace {

using namespace std::chrono_literals;

class DispatcherTest : public ::testing::Test {
protected:
    /// Runs a command and returns the RESP-encoded reply, i.e. exactly what a client receives.
    std::string run(std::vector<std::string_view> argv) {
        std::string out;
        protocol::encode(dispatcher_.dispatch(argv), out);
        return out;
    }

    testing::ManualClock clock_;
    storage::Database db_{clock_};
    Dispatcher dispatcher_{db_};
};

TEST_F(DispatcherTest, PingAndEcho) {
    EXPECT_EQ(run({"PING"}), "+PONG\r\n");
    EXPECT_EQ(run({"ping", "hi"}), "$2\r\nhi\r\n");
    EXPECT_EQ(run({"ECHO", "x"}), "$1\r\nx\r\n");
}

TEST_F(DispatcherTest, CommandNamesAreCaseInsensitive) {
    EXPECT_EQ(run({"sEt", "k", "v"}), "+OK\r\n");
    EXPECT_EQ(run({"GeT", "k"}), "$1\r\nv\r\n");
}

TEST_F(DispatcherTest, RejectsUnknownCommandsAndBadArity) {
    EXPECT_EQ(run({"NOPE"}), "-ERR unknown command 'NOPE'\r\n");
    EXPECT_EQ(run({"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(run({"GET", "a", "b"}), "-ERR wrong number of arguments for 'get' command\r\n");
}

TEST_F(DispatcherTest, StringCommands) {
    EXPECT_EQ(run({"GET", "k"}), "$-1\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "NX"}), "+OK\r\n");
    EXPECT_EQ(run({"SET", "k", "v2", "NX"}), "$-1\r\n");
    EXPECT_EQ(run({"SET", "k", "v3", "XX", "PX", "500"}), "+OK\r\n");
    EXPECT_EQ(run({"PTTL", "k"}), ":500\r\n");
    EXPECT_EQ(run({"TTL", "k"}), ":1\r\n");  // rounded like Redis
    EXPECT_EQ(run({"TYPE", "k"}), "+string\r\n");
    EXPECT_EQ(run({"DEL", "k", "k", "other"}), ":1\r\n");
}

TEST_F(DispatcherTest, SetRejectsInvalidOptions) {
    EXPECT_EQ(run({"SET", "k", "v", "EX", "0"}), "-ERR invalid expire time in 'set' command\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "EX", "abc"}),
              "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "NX", "XX"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "EX", "1", "PX", "1"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "EX"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "k", "v", "EX", "999999999999999"}),
              "-ERR invalid expire time in 'set' command\r\n");
}

TEST_F(DispatcherTest, ExpirationCommands) {
    run({"SET", "k", "v"});
    EXPECT_EQ(run({"TTL", "k"}), ":-1\r\n");
    EXPECT_EQ(run({"TTL", "missing"}), ":-2\r\n");
    EXPECT_EQ(run({"EXPIRE", "k", "10"}), ":1\r\n");
    EXPECT_EQ(run({"TTL", "k"}), ":10\r\n");
    EXPECT_EQ(run({"PERSIST", "k"}), ":1\r\n");
    EXPECT_EQ(run({"PEXPIRE", "k", "100"}), ":1\r\n");
    clock_.advance(100ms);
    EXPECT_EQ(run({"EXISTS", "k"}), ":0\r\n");
}

TEST_F(DispatcherTest, SortedSetCommands) {
    EXPECT_EQ(run({"ZADD", "lb", "1250.5", "Player_One", "890", "Player_Two", "2400", "Pro_Gamer"}),
              ":3\r\n");
    EXPECT_EQ(run({"ZADD", "lb", "100", "Player_One"}), ":0\r\n");
    EXPECT_EQ(run({"ZSCORE", "lb", "Player_One"}), "$3\r\n100\r\n");
    EXPECT_EQ(run({"ZRANK", "lb", "Pro_Gamer"}), ":2\r\n");
    EXPECT_EQ(run({"ZRANK", "lb", "nobody"}), "$-1\r\n");
    EXPECT_EQ(run({"ZCARD", "lb"}), ":3\r\n");
    EXPECT_EQ(run({"ZRANGE", "lb", "0", "-1"}),
              "*3\r\n$10\r\nPlayer_One\r\n$10\r\nPlayer_Two\r\n$9\r\nPro_Gamer\r\n");
    EXPECT_EQ(run({"ZRANGE", "lb", "-1", "-1", "WITHSCORES"}),
              "*2\r\n$9\r\nPro_Gamer\r\n$4\r\n2400\r\n");
    EXPECT_EQ(run({"ZRANGEBYSCORE", "lb", "(100", "+inf", "withscores"}),
              "*4\r\n$10\r\nPlayer_Two\r\n$3\r\n890\r\n$9\r\nPro_Gamer\r\n$4\r\n2400\r\n");
    EXPECT_EQ(run({"ZREM", "lb", "Player_One", "ghost"}), ":1\r\n");
    EXPECT_EQ(run({"TYPE", "lb"}), "+zset\r\n");
}

TEST_F(DispatcherTest, ZaddIsAllOrNothing) {
    EXPECT_EQ(run({"ZADD", "z", "1", "a", "nan", "b"}), "-ERR value is not a valid float\r\n");
    EXPECT_EQ(run({"EXISTS", "z"}), ":0\r\n");
    EXPECT_EQ(run({"ZADD", "z", "1", "a", "2"}), "-ERR syntax error\r\n");
}

TEST_F(DispatcherTest, ReportsWrongType) {
    run({"SET", "s", "v"});
    EXPECT_EQ(run({"ZADD", "s", "1", "m"}),
              "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
}

TEST_F(DispatcherTest, InfoAndDbsize) {
    run({"SET", "a", "1"});
    run({"SET", "b", "2", "EX", "100"});
    run({"GET", "a"});
    EXPECT_EQ(run({"DBSIZE"}), ":2\r\n");
    const std::string info = run({"INFO"});
    EXPECT_NE(info.find("keyspace_hits:1"), std::string::npos);
    EXPECT_NE(info.find("db0:keys=2,expires=1"), std::string::npos);
    EXPECT_EQ(run({"FLUSHALL"}), "+OK\r\n");
    EXPECT_EQ(run({"DBSIZE"}), ":0\r\n");
}

}  // namespace
}  // namespace memkv::command
