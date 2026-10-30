// Commands that work on any key regardless of its type:
// DEL, EXISTS, TYPE, EXPIRE, PEXPIRE, TTL, PTTL, PERSIST.

#include <chrono>

#include "memkv/command/command.hpp"
#include "memkv/command/commands/builtin_commands.hpp"
#include "memkv/command/replies.hpp"

namespace memkv::command {
namespace {

using protocol::Reply;
using storage::Database;

/// DEL key [key ...]: returns how many keys existed and were removed.
class DelCommand final : public Command {
public:
    DelCommand() : Command("del", Arity::at_least(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        std::size_t deleted = 0;
        for (const std::string_view key : args.from(0)) {
            if (db.del(key)) {
                ++deleted;
            }
        }
        return replies::count(deleted);
    }
};

/// EXISTS key [key ...]: returns how many of the keys exist (repeats count again).
class ExistsCommand final : public Command {
public:
    ExistsCommand() : Command("exists", Arity::at_least(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        std::size_t found = 0;
        for (const std::string_view key : args.from(0)) {
            if (db.exists(key)) {
                ++found;
            }
        }
        return replies::count(found);
    }
};

/// TYPE key
class TypeCommand final : public Command {
public:
    TypeCommand() : Command("type", Arity::exactly(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        switch (db.type(args.text(0))) {
            case storage::ValueType::kString:
                return Reply::simple("string");
            case storage::ValueType::kSortedSet:
                return Reply::simple("zset");
            case storage::ValueType::kNone:
                break;
        }
        return Reply::simple("none");
    }
};

/// EXPIRE key seconds / PEXPIRE key milliseconds
class ExpireCommand final : public Command {
public:
    ExpireCommand(std::string_view name, Duration unit)
        : Command(name, Arity::exactly(2)), unit_(unit) {}

    Reply execute(Database& db, const Arguments& args) const override {
        return replies::boolean(db.expire(args.text(0), args.ttl(1, unit_, name())));
    }

private:
    Duration unit_;
};

/// TTL key / PTTL key
class TtlCommand final : public Command {
public:
    TtlCommand(std::string_view name, bool in_seconds)
        : Command(name, Arity::exactly(1)), in_seconds_(in_seconds) {}

    Reply execute(Database& db, const Arguments& args) const override {
        return replies::time_to_live(db.ttl(args.text(0)), in_seconds_);
    }

private:
    bool in_seconds_;
};

/// PERSIST key: removes the key's TTL.
class PersistCommand final : public Command {
public:
    PersistCommand() : Command("persist", Arity::exactly(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        return replies::boolean(db.persist(args.text(0)));
    }
};

}  // namespace

void register_key_commands(CommandRegistry& registry) {
    registry.add<DelCommand>();
    registry.add<ExistsCommand>();
    registry.add<TypeCommand>();
    registry.add(std::make_unique<ExpireCommand>("expire", std::chrono::seconds(1)));
    registry.add(std::make_unique<ExpireCommand>("pexpire", std::chrono::milliseconds(1)));
    registry.add(std::make_unique<TtlCommand>("ttl", /*in_seconds=*/true));
    registry.add(std::make_unique<TtlCommand>("pttl", /*in_seconds=*/false));
    registry.add<PersistCommand>();
}

}  // namespace memkv::command
