#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace memkv::net {

/// Owning wrapper around a POSIX file descriptor; closes it on destruction.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    void reset(int fd = -1) noexcept;

private:
    int fd_ = -1;
};

/// Creates a non-blocking TCP socket listening on host:port. Port 0 lets the OS pick a free
/// port (see local_port()). Throws std::system_error on failure.
UniqueFd listen_tcp(const std::string& host, std::uint16_t port, int backlog = 511);

/// Accepts one pending connection and prepares it for the event loop (non-blocking, TCP_NODELAY).
/// Returns an invalid UniqueFd when no connection is pending; throws on hard errors.
UniqueFd accept_client(int listener_fd);

/// The local port a socket is bound to.
std::uint16_t local_port(int fd);

/// Flags for send(2) that suppress SIGPIPE where the platform supports it per call.
int send_flags() noexcept;

}  // namespace memkv::net
