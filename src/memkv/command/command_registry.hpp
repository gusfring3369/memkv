#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "memkv/command/command.hpp"

namespace memkv::command {

/// Owns every known command and finds them by name, ignoring case.
class CommandRegistry {
public:
    /// A registry holding every command memkv supports.
    [[nodiscard]] static CommandRegistry with_builtin_commands();

    /// Adds a command. Throws std::logic_error if one with the same name already exists.
    void add(std::unique_ptr<Command> command);

    /// Convenience: `registry.add<GetCommand>()`.
    template <typename CommandType>
    void add() {
        add(std::make_unique<CommandType>());
    }

    /// The command called `name` (any case), or nullptr if there is none.
    [[nodiscard]] const Command* find(std::string_view name) const;

    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;  // key: lower-case name
};

}  // namespace memkv::command
