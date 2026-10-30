#include "server_options.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace memkv::app {
namespace {

constexpr std::string_view kUsage =
    "Usage: memkv-server [options]\n"
    "\n"
    "Options:\n"
    "  --host <address>   Address to bind (default 127.0.0.1)\n"
    "  --port <port>      TCP port to listen on (default 6380)\n"
    "  --max-keys <n>     Evict least recently used keys beyond n keys (default 0 = unbounded)\n"
    "  --help             Show this message\n";

template <typename Number>
std::optional<Number> parse_number(std::string_view text) {
    Number value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[noreturn]] void exit_with_usage(const std::string& problem) {
    std::fprintf(stderr, "memkv-server: %s\n\n%.*s", problem.c_str(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    std::exit(EXIT_FAILURE);
}

template <typename Number>
Number require_number(std::string_view flag, std::string_view value) {
    const std::optional<Number> number = parse_number<Number>(value);
    if (!number) {
        exit_with_usage("invalid value for " + std::string(flag) + ": " + std::string(value));
    }
    return *number;
}

}  // namespace

ServerOptions parse_command_line(int argc, char** argv) {
    ServerOptions options;
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
            options.server.port = require_number<std::uint16_t>(flag, value);
        } else if (flag == "--max-keys") {
            options.keyspace.max_keys = require_number<std::size_t>(flag, value);
        } else {
            exit_with_usage("unknown option " + std::string(flag));
        }
    }
    return options;
}

}  // namespace memkv::app
