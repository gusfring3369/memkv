#include "memkv/net/connection.hpp"

#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cerrno>
#include <string_view>

#include "memkv/command/dispatcher.hpp"
#include "memkv/protocol/reply.hpp"

namespace memkv::net {
namespace {

constexpr std::size_t kReadChunk = std::size_t{16} * 1024;
// Cap per readiness event so one fast client cannot monopolise the event loop.
constexpr std::size_t kMaxBytesPerReceive = std::size_t{1024} * 1024;
// Once this much sent output accumulates at the front of the buffer, reclaim it.
constexpr std::size_t kCompactOutputThreshold = std::size_t{64} * 1024;

bool would_block(int error) {
    return error == EAGAIN || error == EWOULDBLOCK;
}

bool is_quit(std::string_view name) {
    return name.size() == 4 && (name[0] | 0x20) == 'q' && (name[1] | 0x20) == 'u' &&
           (name[2] | 0x20) == 'i' && (name[3] | 0x20) == 't';
}

}  // namespace

Connection::IoStatus Connection::receive() {
    std::array<char, kReadChunk> buffer;  // deliberately uninitialized: recv() fills it
    std::size_t total = 0;
    while (total < kMaxBytesPerReceive) {
        const ssize_t n = ::recv(socket_.get(), buffer.data(), buffer.size(), 0);
        if (n > 0) {
            input_.append(buffer.data(), static_cast<std::size_t>(n));
            total += static_cast<std::size_t>(n);
            if (static_cast<std::size_t>(n) < buffer.size()) {
                break;  // socket drained; skip the extra recv() that would only say EAGAIN
            }
        } else if (n == 0) {
            return IoStatus::kClosed;
        } else if (errno == EINTR) {
            continue;
        } else if (would_block(errno)) {
            break;
        } else {
            return IoStatus::kClosed;
        }
    }
    return IoStatus::kOpen;
}

void Connection::process_requests(command::Dispatcher& dispatcher) {
    using Status = protocol::RequestParser::Status;
    std::size_t offset = 0;
    while (!closing_) {
        const auto [status, consumed] =
            parser_.parse(std::string_view(input_).substr(offset), args_);
        offset += consumed;
        if (status == Status::kNeedMoreData) {
            break;
        }
        if (status == Status::kProtocolError) {
            protocol::encode(protocol::Reply::error("ERR Protocol error: " + parser_.error()),
                             output_);
            closing_ = true;
            break;
        }
        if (is_quit(args_.front())) {
            protocol::encode(protocol::Reply::ok(), output_);
            closing_ = true;
            break;
        }
        protocol::encode(dispatcher.dispatch(args_), output_);
    }
    input_.erase(0, offset);
}

Connection::IoStatus Connection::flush() {
    while (has_pending_output()) {
        const ssize_t n =
            ::send(socket_.get(), output_.data() + sent_, output_.size() - sent_, send_flags());
        if (n >= 0) {
            sent_ += static_cast<std::size_t>(n);
        } else if (errno == EINTR) {
            continue;
        } else if (would_block(errno)) {
            break;
        } else {
            return IoStatus::kClosed;
        }
    }
    if (!has_pending_output()) {
        output_.clear();
        sent_ = 0;
    } else if (sent_ >= kCompactOutputThreshold) {
        output_.erase(0, sent_);
        sent_ = 0;
    }
    return IoStatus::kOpen;
}

}  // namespace memkv::net
