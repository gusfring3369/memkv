#include "memkv/net/socket.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <system_error>

namespace memkv::net {
namespace {

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

void set_non_blocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw_errno("fcntl(O_NONBLOCK)");
    }
}

void set_option(int fd, int level, int option) {
    const int enabled = 1;
    if (::setsockopt(fd, level, option, &enabled, sizeof(enabled)) < 0) {
        throw_errno("setsockopt");
    }
}

}  // namespace

void UniqueFd::reset(int fd) noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

UniqueFd listen_tcp(const std::string& host, std::uint16_t port, int backlog) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* raw_results = nullptr;
    const std::string service = std::to_string(port);
    if (const int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_results);
        rc != 0) {
        throw std::runtime_error("cannot resolve '" + host + "': " + ::gai_strerror(rc));
    }
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(raw_results,
                                                                       &::freeaddrinfo);

    int last_error = 0;
    for (const addrinfo* ai = results.get(); ai != nullptr; ai = ai->ai_next) {
        UniqueFd fd(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!fd.valid()) {
            last_error = errno;
            continue;
        }
        set_option(fd.get(), SOL_SOCKET, SO_REUSEADDR);
        if (::bind(fd.get(), ai->ai_addr, ai->ai_addrlen) == 0 &&
            ::listen(fd.get(), backlog) == 0) {
            set_non_blocking(fd.get());
            return fd;
        }
        last_error = errno;
    }
    throw std::system_error(last_error, std::generic_category(),
                            "cannot listen on " + host + ":" + service);
}

UniqueFd accept_client(int listener_fd) {
    while (true) {
        UniqueFd fd(::accept(listener_fd, nullptr, nullptr));
        if (fd.valid()) {
            set_non_blocking(fd.get());
            set_option(fd.get(), IPPROTO_TCP, TCP_NODELAY);  // replies are small; don't batch
#ifdef SO_NOSIGPIPE
            set_option(fd.get(), SOL_SOCKET, SO_NOSIGPIPE);
#endif
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        // ECONNABORTED: the client gave up while queued; nothing to accept after all.
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED) {
            return UniqueFd();
        }
        throw_errno("accept");
    }
}

std::uint16_t local_port(int fd) {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        throw_errno("getsockname");
    }
    if (address.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    }
    return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
}

int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;  // SO_NOSIGPIPE is set on the socket instead
#endif
}

}  // namespace memkv::net
