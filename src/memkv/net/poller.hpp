#pragma once

#include <chrono>
#include <memory>
#include <span>

namespace memkv::net {

enum class Interest { kRead, kReadWrite };

struct PollEvent {
    int fd = -1;
    /// Also set on hang-up or error, so that the subsequent read observes EOF or the error.
    bool readable = false;
    bool writable = false;
};

/// Level-triggered readiness notification for a set of file descriptors.
///
/// Like Redis' "ae" library, the interface hides the OS mechanism: epoll(7) on Linux and the
/// portable poll(2) everywhere else. The backend is chosen at build time.
class Poller {
public:
    Poller();
    ~Poller();
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    void add(int fd, Interest interest);
    void modify(int fd, Interest interest);
    void remove(int fd);

    /// Blocks until at least one descriptor is ready or `timeout` elapses. The returned events
    /// stay valid until the next call to wait(); add/modify/remove do not invalidate them.
    std::span<const PollEvent> wait(std::chrono::milliseconds timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace memkv::net
