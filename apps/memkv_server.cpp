// memkv-server: command-line entry point.

#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include "memkv/common/clock.hpp"
#include "memkv/common/log.hpp"
#include "memkv/net/server.hpp"
#include "memkv/storage/database.hpp"

namespace {

struct Options {
    memkv::net::ServerConfig server;
    memkv::storage::KeyspaceConfig keyspace;
};

constexpr std::string_view kUsage =
    "Usage: memkv-server [options]\n"
    "\n"
    "Options:\n"
    "  --host <address>   Address to bind (default 127.0.0.1)\n"
    "  --port <port>      TCP port to listen on (default 6380)\n"
    "  --max-keys <n>     Evict least recently used keys beyond n keys (default 0 = unbounded)\n"
    "  --help             Show this message\n";

template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[noreturn]] void exit_with_usage(std::string_view error) {
    std::fprintf(stderr, "memkv-server: %.*s\n\n%.*s", static_cast<int>(error.size()), error.data(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    std::exit(EXIT_FAILURE);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            std::fputs(kUsage.data(), stdout);
            std::exit(EXIT_SUCCESS);
        }
        if (i + 1 >= argc) {
            exit_with_usage("missing value for " + std::string(flag));
        }
        const std::string_view value = argv[++i];
        if (flag == "--host") {
            options.server.host = value;
        } else if (flag == "--port") {
            const auto port = parse_number<std::uint16_t>(value);
            if (!port) {
                exit_with_usage("invalid port: " + std::string(value));
            }
            options.server.port = *port;
        } else if (flag == "--max-keys") {
            const auto max_keys = parse_number<std::size_t>(value);
            if (!max_keys) {
                exit_with_usage("invalid key limit: " + std::string(value));
            }
            options.keyspace.max_keys = *max_keys;
        } else {
            exit_with_usage("unknown option " + std::string(flag));
        }
    }
    return options;
}

memkv::net::Server* g_server = nullptr;

extern "C" void handle_shutdown_signal(int /*signal*/) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    try {
        const memkv::SteadyClock clock;
        memkv::storage::Database db(clock, options.keyspace);
        memkv::net::Server server(options.server, db);

        g_server = &server;
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);

        memkv::log::info("memkv listening on " + options.server.host + ":" +
                         std::to_string(server.port()));
        server.run();
        g_server = nullptr;
        memkv::log::info("shutdown complete");
    } catch (const std::exception& e) {
        memkv::log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
