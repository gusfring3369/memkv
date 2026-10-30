#include "memkv/command/dispatcher.hpp"

#include <string>

#include "memkv/command/command_error.hpp"

namespace memkv::command {
namespace {

using protocol::Reply;

constexpr std::size_t kMaxEchoedNameLength = 128;

Reply unknown_command(std::string_view name) {
    return Reply::error("ERR unknown command '" +
                        std::string(name.substr(0, kMaxEchoedNameLength)) + "'");
}

Reply wrong_number_of_arguments(const Command& command) {
    return Reply::error("ERR wrong number of arguments for '" + command.name() + "' command");
}

}  // namespace

Dispatcher::Dispatcher(storage::Database& db)
    : db_(db), registry_(CommandRegistry::with_builtin_commands()) {}

Reply Dispatcher::dispatch(std::span<const std::string_view> argv) {
    if (argv.empty()) {
        return Reply::error("ERR empty command");
    }
    const Command* command = registry_.find(argv.front());
    if (command == nullptr) {
        return unknown_command(argv.front());
    }
    const Arguments args(argv.subspan(1));
    if (!command->arity().accepts(args.size())) {
        return wrong_number_of_arguments(*command);
    }

    // The single place where errors raised while executing a command become replies.
    try {
        return command->execute(db_, args);
    } catch (const storage::WrongTypeError&) {
        return Reply::error("WRONGTYPE Operation against a key holding the wrong kind of value");
    } catch (const CommandError& error) {
        return Reply::error(std::string("ERR ") + error.what());
    }
}

}  // namespace memkv::command
