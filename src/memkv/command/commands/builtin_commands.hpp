#pragma once

#include "memkv/command/command_registry.hpp"

/// Each command family lives in its own source file and exposes one function that adds its
/// commands to a registry. CommandRegistry::with_builtin_commands() calls all of them.
namespace memkv::command {

void register_server_commands(CommandRegistry& registry);      // PING, ECHO, INFO, ...
void register_key_commands(CommandRegistry& registry);         // DEL, EXPIRE, TTL, ...
void register_string_commands(CommandRegistry& registry);      // GET, SET
void register_sorted_set_commands(CommandRegistry& registry);  // ZADD, ZRANGE, ...

}  // namespace memkv::command
