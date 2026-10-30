// Server-level commands: PING, ECHO, DBSIZE, FLUSHALL, FLUSHDB, INFO, COMMAND, CONFIG.

#include <string>

#include "memkv/command/command.hpp"
#include "memkv/command/commands/builtin_commands.hpp"
#include "memkv/command/replies.hpp"

namespace memkv::command {
namespace {

using protocol::Reply;
using storage::Database;

/// PING [message]
class PingCommand final : public Command {
public:
    PingCommand() : Command("ping", Arity::between(0, 1)) {}

    Reply execute(Database& /*db*/, const Arguments& args) const override {
        return args.size() == 0 ? Reply::simple("PONG") : Reply::bulk(args.string(0));
    }
};

/// ECHO message
class EchoCommand final : public Command {
public:
    EchoCommand() : Command("echo", Arity::exactly(1)) {}

    Reply execute(Database& /*db*/, const Arguments& args) const override {
        return Reply::bulk(args.string(0));
    }
};

/// DBSIZE
class DbSizeCommand final : public Command {
public:
    DbSizeCommand() : Command("dbsize", Arity::exactly(0)) {}

    Reply execute(Database& db, const Arguments& /*args*/) const override {
        return replies::count(db.size());
    }
};

/// FLUSHALL / FLUSHDB [ASYNC | SYNC]: memkv has one database, so both clear everything.
class FlushCommand final : public Command {
public:
    explicit FlushCommand(std::string_view name) : Command(name, Arity::between(0, 1)) {}

    Reply execute(Database& db, const Arguments& /*args*/) const override {
        db.flush();
        return Reply::ok();
    }
};

/// INFO: statistics in Redis' "key:value" text format.
class InfoCommand final : public Command {
public:
    InfoCommand() : Command("info", Arity::at_least(0)) {}

    Reply execute(Database& db, const Arguments& /*args*/) const override {
        const storage::DatabaseStats stats = db.stats();
        std::string info;
        info += "# Stats\r\n";
        info += "keyspace_hits:" + std::to_string(stats.hits) + "\r\n";
        info += "keyspace_misses:" + std::to_string(stats.misses) + "\r\n";
        info += "expired_keys:" + std::to_string(stats.expired_keys) + "\r\n";
        info += "evicted_keys:" + std::to_string(stats.evicted_keys) + "\r\n";
        info += "\r\n# Keyspace\r\n";
        info += "db0:keys=" + std::to_string(stats.keys) +
                ",expires=" + std::to_string(stats.keys_with_expiry) + "\r\n";
        return Reply::bulk(std::move(info));
    }
};

/// COMMAND and CONFIG are not implemented. redis-cli and redis-benchmark probe them when they
/// connect, and an empty reply lets them carry on.
class EmptyReplyCommand final : public Command {
public:
    EmptyReplyCommand(std::string_view name, Arity arity) : Command(name, arity) {}

    Reply execute(Database& /*db*/, const Arguments& /*args*/) const override {
        return Reply::array({});
    }
};

}  // namespace

void register_server_commands(CommandRegistry& registry) {
    registry.add<PingCommand>();
    registry.add<EchoCommand>();
    registry.add<DbSizeCommand>();
    registry.add(std::make_unique<FlushCommand>("flushall"));
    registry.add(std::make_unique<FlushCommand>("flushdb"));
    registry.add<InfoCommand>();
    registry.add(std::make_unique<EmptyReplyCommand>("command", Arity::at_least(0)));
    registry.add(std::make_unique<EmptyReplyCommand>("config", Arity::at_least(1)));
}

}  // namespace memkv::command
