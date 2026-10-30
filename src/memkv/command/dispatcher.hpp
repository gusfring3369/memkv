#pragma once

#include <span>
#include <string_view>

#include "memkv/protocol/reply.hpp"
#include "memkv/storage/database.hpp"

namespace memkv::command {

/// Translates requests into Database operations.
///
/// This layer owns everything request-shaped: command lookup (case-insensitive), arity checks,
/// parsing numbers and options, and mapping results and domain errors onto RESP replies. The
/// storage layer below it therefore only ever sees well-typed, validated arguments.
class Dispatcher {
public:
    explicit Dispatcher(storage::Database& db) : db_(db) {}

    /// Executes `argv` (argv[0] is the command name) and returns the reply to send.
    [[nodiscard]] protocol::Reply dispatch(std::span<const std::string_view> argv);

private:
    storage::Database& db_;
};

}  // namespace memkv::command
