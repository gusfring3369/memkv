#include "memkv/net/server.hpp"

#include <algorithm>
#include <exception>
#include <string>

#include "memkv/common/log.hpp"

namespace memkv::net {

Server::Server(ServerConfig config, storage::Database& db)
    : config_(std::move(config)),
      db_(db),
      dispatcher_(db),
      listener_(listen_tcp(config_.host, config_.port)),
      port_(local_port(listener_.get())) {
    poller_.add(listener_.get(), Interest::kRead);
}

Server::~Server() = default;

void Server::run() {
    using SteadyClock = std::chrono::steady_clock;
    auto next_cron = SteadyClock::now() + config_.cron_interval;

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        const auto until_cron =
            std::chrono::duration_cast<std::chrono::milliseconds>(next_cron - SteadyClock::now());
        const auto timeout =
            std::clamp(until_cron, std::chrono::milliseconds::zero(), config_.cron_interval);

        for (const PollEvent& event : poller_.wait(timeout)) {
            if (event.fd == listener_.get()) {
                accept_clients();
            } else {
                handle_client_event(event);
            }
        }

        if (SteadyClock::now() >= next_cron) {
            run_cron();
            next_cron = SteadyClock::now() + config_.cron_interval;
        }
    }

    while (!clients_.empty()) {
        disconnect(clients_.begin()->first);
    }
}

void Server::accept_clients() {
    while (true) {
        UniqueFd socket;
        try {
            socket = accept_client(listener_.get());
        } catch (const std::exception& e) {
            // Typically EMFILE/ENFILE: keep serving existing clients and retry on the next event.
            log::warn(std::string("accept failed: ") + e.what());
            return;
        }
        if (!socket.valid()) {
            return;  // no more pending connections
        }
        const int fd = socket.get();
        poller_.add(fd, Interest::kRead);
        clients_.emplace(fd, std::make_unique<Connection>(std::move(socket)));
    }
}

void Server::handle_client_event(const PollEvent& event) {
    const auto it = clients_.find(event.fd);
    if (it == clients_.end()) {
        return;  // disconnected earlier in this batch of events
    }
    Connection& client = *it->second;
    if (!service_client(client, event)) {
        disconnect(event.fd);
        return;
    }
    update_write_interest(client);
}

bool Server::service_client(Connection& client, const PollEvent& event) {
    bool peer_closed = false;
    if (event.readable) {
        peer_closed = client.receive() == Connection::IoStatus::kClosed;
        // Run whatever arrived before the EOF; a client may send a command and half-close.
        client.process_requests(dispatcher_);
    }
    if (client.has_pending_output() && client.flush() == Connection::IoStatus::kClosed) {
        return false;
    }
    if (peer_closed || (client.closing() && !client.has_pending_output())) {
        return false;
    }
    if (client.pending_output_bytes() > config_.max_output_buffer_bytes) {
        log::warn("disconnecting client that is not reading its replies");
        return false;
    }
    return true;
}

void Server::update_write_interest(Connection& client) {
    // Only watch for writability while output is queued; otherwise a level-triggered poller
    // would report the (almost always writable) socket on every iteration.
    const bool want_writes = client.has_pending_output();
    if (want_writes != client.watching_writes()) {
        poller_.modify(client.fd(), want_writes ? Interest::kReadWrite : Interest::kRead);
        client.set_watching_writes(want_writes);
    }
}

void Server::disconnect(int fd) {
    poller_.remove(fd);
    clients_.erase(fd);  // closes the socket
}

void Server::run_cron() {
    db_.active_expire(config_.expire_checks_per_cron);
}

}  // namespace memkv::net
