#include "memkv/command/command_registry.hpp"

#include <stdexcept>

#include "memkv/command/commands/builtin_commands.hpp"

namespace memkv::command {

CommandRegistry CommandRegistry::with_builtin_commands() {
    CommandRegistry registry;
    register_server_commands(registry);
    register_key_commands(registry);
    register_string_commands(registry);
    register_sorted_set_commands(registry);
    return registry;
}

void CommandRegistry::add(std::unique_ptr<Command> command) {
    std::string key = to_lower(command->name());
    const auto [it, inserted] = commands_.emplace(std::move(key), std::move(command));
    if (!inserted) {
        throw std::logic_error("command registered twice: " + it->first);
    }
}

const Command* CommandRegistry::find(std::string_view name) const {
    const auto it = commands_.find(to_lower(name));
    return it == commands_.end() ? nullptr : it->second.get();
}

}  // namespace memkv::command
