// epoll(7) backend for Poller (Linux).

#include <sys/epoll.h>

#include <cerrno>
#include <system_error>
#include <vector>

#include "memkv/net/poller.hpp"
#include "memkv/net/socket.hpp"

namespace memkv::net {
namespace {

constexpr std::size_t kInitialEventCapacity = 256;

std::uint32_t to_epoll_mask(Interest interest) {
    return interest == Interest::kReadWrite ? (EPOLLIN | EPOLLOUT) : EPOLLIN;
}

void control(int epoll_fd, int operation, int fd, Interest interest) {
    epoll_event event{};
    event.events = to_epoll_mask(interest);
    event.data.fd = fd;
    if (::epoll_ctl(epoll_fd, operation, fd, &event) < 0) {
        throw std::system_error(errno, std::generic_category(), "epoll_ctl");
    }
}

}  // namespace

struct Poller::Impl {
    UniqueFd epoll_fd;
    std::vector<epoll_event> raw_events = std::vector<epoll_event>(kInitialEventCapacity);
    std::vector<PollEvent> ready;
};

Poller::Poller() : impl_(std::make_unique<Impl>()) {
    impl_->epoll_fd.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!impl_->epoll_fd.valid()) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

Poller::~Poller() = default;

void Poller::add(int fd, Interest interest) {
    control(impl_->epoll_fd.get(), EPOLL_CTL_ADD, fd, interest);
}

void Poller::modify(int fd, Interest interest) {
    control(impl_->epoll_fd.get(), EPOLL_CTL_MOD, fd, interest);
}

void Poller::remove(int fd) {
    // Failure only means the descriptor was not registered, which is harmless here.
    ::epoll_ctl(impl_->epoll_fd.get(), EPOLL_CTL_DEL, fd, nullptr);
}

std::span<const PollEvent> Poller::wait(std::chrono::milliseconds timeout) {
    impl_->ready.clear();
    const int count =
        ::epoll_wait(impl_->epoll_fd.get(), impl_->raw_events.data(),
                     static_cast<int>(impl_->raw_events.size()), static_cast<int>(timeout.count()));
    if (count < 0) {
        if (errno == EINTR) {
            return {};
        }
        throw std::system_error(errno, std::generic_category(), "epoll_wait");
    }

    const auto ready_count = static_cast<std::size_t>(count);
    for (std::size_t i = 0; i < ready_count; ++i) {
        const epoll_event& raw = impl_->raw_events[i];
        impl_->ready.push_back({
            .fd = raw.data.fd,
            .readable = (raw.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0,
            .writable = (raw.events & EPOLLOUT) != 0,
        });
    }
    if (ready_count == impl_->raw_events.size()) {
        impl_->raw_events.resize(ready_count * 2);  // more may have been ready; grow for next time
    }
    return impl_->ready;
}

}  // namespace memkv::net
