#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "memkv/command/dispatcher.hpp"
#include "memkv/net/connection.hpp"
#include "memkv/net/poller.hpp"
#include "memkv/net/socket.hpp"
#include "memkv/storage/database.hpp"

namespace memkv::net {

struct ServerConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 6380;
    /// How often background work (active expiration) runs.
    std::chrono::milliseconds cron_interval{100};
    /// Expiry-heap items examined per cron tick; bounds the pause a mass expiry can cause.
    std::size_t expire_checks_per_cron = 1000;
    /// Clients that stop reading their replies are disconnected beyond this much queued output.
    std::size_t max_output_buffer_bytes = 64 * 1024 * 1024;
};

/// Single-threaded, event-driven TCP server: the reactor design Redis uses.
///
/// One thread multiplexes every client socket through a Poller and executes commands one at a
/// time, so the storage engine needs no locks and each command is atomic. Network I/O is
/// non-blocking; a periodic "cron" step performs active expiration between events.
class Server {
public:
    /// Binds and listens immediately, so port() is known before run() is called.
    Server(ServerConfig config, storage::Database& db);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    /// Runs the event loop until stop() is called.
    void run();

    /// Asks run() to return within one cron interval. Safe to call from another thread or from
    /// a signal handler (it only performs a lock-free atomic store).
    void stop() noexcept { stop_requested_.store(true, std::memory_order_relaxed); }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

private:
    void accept_clients();
    void handle_client_event(const PollEvent& event);
    /// Returns false if the client should be disconnected.
    bool service_client(Connection& client, const PollEvent& event);
    void update_write_interest(Connection& client);
    void disconnect(int fd);
    void run_cron();

    ServerConfig config_;
    storage::Database& db_;
    command::Dispatcher dispatcher_;
    UniqueFd listener_;
    std::uint16_t port_ = 0;
    Poller poller_;
    std::unordered_map<int, std::unique_ptr<Connection>> clients_;
    std::atomic<bool> stop_requested_{false};

    static_assert(std::atomic<bool>::is_always_lock_free, "stop() must be async-signal-safe");
};

}  // namespace memkv::net
