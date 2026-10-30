// String commands: GET, SET.

#include <chrono>
#include <optional>
#include <string>

#include "memkv/command/command.hpp"
#include "memkv/command/command_error.hpp"
#include "memkv/command/commands/builtin_commands.hpp"

namespace memkv::command {
namespace {

using protocol::Reply;
using storage::Database;
using storage::SetCondition;
using storage::SetOptions;

/// GET key
class GetCommand final : public Command {
public:
    GetCommand() : Command("get", Arity::exactly(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        std::optional<std::string> value = db.get(args.text(0));
        return value ? Reply::bulk(std::move(*value)) : Reply::null();
    }
};

/// SET key value [NX | XX] [EX seconds | PX milliseconds]
class SetCommand final : public Command {
public:
    SetCommand() : Command("set", Arity::at_least(2)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const SetOptions options = parse_options(args);
        const bool written = db.set(args.text(0), args.string(1), options);
        return written ? Reply::ok() : Reply::null();  // null: the NX/XX condition failed
    }

private:
    /// Reads the optional flags that follow `key value`.
    static SetOptions parse_options(const Arguments& args) {
        SetOptions options;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args.is(i, "nx") || args.is(i, "xx")) {
                const SetCondition condition =
                    args.is(i, "nx") ? SetCondition::kIfAbsent : SetCondition::kIfPresent;
                if (options.condition != SetCondition::kAlways && options.condition != condition) {
                    throw CommandError("syntax error");  // NX and XX together
                }
                options.condition = condition;
            } else if ((args.is(i, "ex") || args.is(i, "px")) && i + 1 < args.size() &&
                       !options.ttl) {
                const Duration unit = args.is(i, "ex") ? Duration(std::chrono::seconds(1))
                                                       : Duration(std::chrono::milliseconds(1));
                const Duration ttl = args.ttl(++i, unit, "set");
                if (ttl <= Duration::zero()) {
                    throw CommandError("invalid expire time in 'set' command");
                }
                options.ttl = ttl;
            } else {
                throw CommandError("syntax error");
            }
        }
        return options;
    }
};

}  // namespace

void register_string_commands(CommandRegistry& registry) {
    registry.add<GetCommand>();
    registry.add<SetCommand>();
}

}  // namespace memkv::command
