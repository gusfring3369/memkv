#include "memkv/common/log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace memkv::log {
namespace {

void write(const char* level, std::string_view message) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(stderr, "%s [%s] %.*s\n", timestamp, level, static_cast<int>(message.size()),
                 message.data());
}

}  // namespace

void info(std::string_view message) {
    write("INFO", message);
}
void warn(std::string_view message) {
    write("WARN", message);
}
void error(std::string_view message) {
    write("ERROR", message);
}

}  // namespace memkv::log
