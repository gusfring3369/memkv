// Portable poll(2) backend for Poller (macOS, BSDs, or Linux with MEMKV_FORCE_POLL).

#include <poll.h>

#include <cerrno>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "memkv/net/poller.hpp"

namespace memkv::net {
namespace {

short to_poll_mask(Interest interest) {
    return interest == Interest::kReadWrite ? static_cast<short>(POLLIN | POLLOUT) : POLLIN;
}

}  // namespace

struct Poller::Impl {
    std::vector<pollfd> descriptors;
    std::unordered_map<int, std::size_t> slot_of;  // fd -> index in `descriptors`
    std::vector<PollEvent> ready;
};

Poller::Poller() : impl_(std::make_unique<Impl>()) {}

Poller::~Poller() = default;

void Poller::add(int fd, Interest interest) {
    impl_->slot_of.emplace(fd, impl_->descriptors.size());
    impl_->descriptors.push_back({fd, to_poll_mask(interest), 0});
}

void Poller::modify(int fd, Interest interest) {
    impl_->descriptors.at(impl_->slot_of.at(fd)).events = to_poll_mask(interest);
}

void Poller::remove(int fd) {
    const auto it = impl_->slot_of.find(fd);
    if (it == impl_->slot_of.end()) {
        return;
    }
    // Swap-and-pop keeps removal O(1).
    const std::size_t slot = it->second;
    impl_->slot_of.erase(it);
    if (slot != impl_->descriptors.size() - 1) {
        impl_->descriptors[slot] = impl_->descriptors.back();
        impl_->slot_of[impl_->descriptors[slot].fd] = slot;
    }
    impl_->descriptors.pop_back();
}

std::span<const PollEvent> Poller::wait(std::chrono::milliseconds timeout) {
    impl_->ready.clear();
    const int count =
        ::poll(impl_->descriptors.data(), static_cast<nfds_t>(impl_->descriptors.size()),
               static_cast<int>(timeout.count()));
    if (count < 0) {
        if (errno == EINTR) {
            return {};
        }
        throw std::system_error(errno, std::generic_category(), "poll");
    }
    for (const pollfd& descriptor : impl_->descriptors) {
        if (descriptor.revents == 0) {
            continue;
        }
        impl_->ready.push_back({
            .fd = descriptor.fd,
            .readable = (descriptor.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0,
            .writable = (descriptor.revents & POLLOUT) != 0,
        });
    }
    return impl_->ready;
}

}  // namespace memkv::net
