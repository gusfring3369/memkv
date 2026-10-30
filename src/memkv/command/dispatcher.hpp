#pragma once

#include <span>
#include <string_view>

#include "memkv/command/command_registry.hpp"
#include "memkv/protocol/reply.hpp"
#include "memkv/storage/database.hpp"

namespace memkv::command {

/// Entry point of the command layer: turns one parsed request into one reply.
///
/// It looks the command up, checks the argument count, runs it, and converts any error the
/// command throws into an error reply. Everything request-specific (parsing numbers and options)
/// happens inside the commands, so the Database below only ever sees validated, typed values.
class Dispatcher {
public:
    explicit Dispatcher(storage::Database& db);

    /// Executes `argv` (argv[0] is the command name) and returns the reply to send.
    [[nodiscard]] protocol::Reply dispatch(std::span<const std::string_view> argv);

private:
    storage::Database& db_;
    CommandRegistry registry_;
};

}  // namespace memkv::command
