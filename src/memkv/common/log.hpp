#pragma once

#include <string_view>

/// Minimal leveled logging to stderr. Only the network and application layers log; the storage
/// engine reports outcomes through return values and counters instead.
namespace memkv::log {

void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}  // namespace memkv::log
