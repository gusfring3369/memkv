#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "memkv/command/dispatcher.hpp"
#include "memkv/net/socket.hpp"
#include "memkv/protocol/request_parser.hpp"

namespace memkv::net {

/// One client: its socket, input waiting to be parsed and output waiting for the socket.
///
/// Requests are processed in arrival order and replies queued in the same order, so pipelined
/// requests (many commands sent without waiting for replies) work naturally.
class Connection {
public:
    enum class IoStatus { kOpen, kClosed };

    explicit Connection(UniqueFd socket) : socket_(std::move(socket)) {}

    [[nodiscard]] int fd() const noexcept { return socket_.get(); }

    /// Reads what the socket has available (bounded per call for fairness between clients).
    /// kClosed means the peer closed the connection or a read error occurred.
    IoStatus receive();

    /// Executes every complete request in the input buffer and queues the replies.
    void process_requests(command::Dispatcher& dispatcher);

    /// Writes as much queued output as the socket accepts without blocking.
    IoStatus flush();

    [[nodiscard]] bool has_pending_output() const noexcept { return sent_ < output_.size(); }
    [[nodiscard]] std::size_t pending_output_bytes() const noexcept {
        return output_.size() - sent_;
    }

    /// True after QUIT or a protocol error: close once the queued output has been sent.
    [[nodiscard]] bool closing() const noexcept { return closing_; }

    /// Whether the event loop is currently watching this socket for writability.
    [[nodiscard]] bool watching_writes() const noexcept { return watching_writes_; }
    void set_watching_writes(bool watching) noexcept { watching_writes_ = watching; }

private:
    UniqueFd socket_;
    protocol::RequestParser parser_;
    std::string input_;
    std::string output_;
    std::size_t sent_ = 0;                // bytes of output_ already written
    std::vector<std::string_view> args_;  // views into input_, valid until it is modified
    bool closing_ = false;
    bool watching_writes_ = false;
};

}  // namespace memkv::net
