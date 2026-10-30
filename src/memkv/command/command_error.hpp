#pragma once

#include <stdexcept>
#include <string>

namespace memkv::command {

/// A mistake in how a command was called: a bad number, an unknown option, and so on.
/// The Dispatcher turns it into an "ERR <message>" reply; the connection stays open.
class CommandError : public std::runtime_error {
public:
    explicit CommandError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace memkv::command
