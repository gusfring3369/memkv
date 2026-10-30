// Sorted set commands: ZADD, ZREM, ZSCORE, ZRANK, ZCARD, ZRANGE, ZRANGEBYSCORE.

#include <optional>
#include <vector>

#include "memkv/command/command.hpp"
#include "memkv/command/command_error.hpp"
#include "memkv/command/commands/builtin_commands.hpp"
#include "memkv/command/replies.hpp"

namespace memkv::command {
namespace {

using protocol::Reply;
using storage::Database;
using storage::ScoredMember;

/// True if the arguments end with the WITHSCORES flag at `position`; throws on anything else.
bool parse_with_scores(const Arguments& args, std::size_t position) {
    if (args.size() == position) {
        return false;
    }
    if (args.size() == position + 1 && args.is(position, "withscores")) {
        return true;
    }
    throw CommandError("syntax error");
}

/// ZADD key score member [score member ...]: returns how many members were new.
class ZAddCommand final : public Command {
public:
    ZAddCommand() : Command("zadd", Arity::at_least(3)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const std::size_t pair_arguments = args.size() - 1;
        if (pair_arguments % 2 != 0) {
            throw CommandError("syntax error");
        }
        // Parse every score before changing anything, so one bad score changes nothing.
        std::vector<ScoredMember> members;
        members.reserve(pair_arguments / 2);
        for (std::size_t i = 1; i < args.size(); i += 2) {
            members.push_back({.member = args.string(i + 1), .score = args.score(i)});
        }
        return replies::count(db.zadd(args.text(0), members));
    }
};

/// ZREM key member [member ...]: returns how many members were removed.
class ZRemCommand final : public Command {
public:
    ZRemCommand() : Command("zrem", Arity::at_least(2)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        return replies::count(db.zrem(args.text(0), args.from(1)));
    }
};

/// ZSCORE key member
class ZScoreCommand final : public Command {
public:
    ZScoreCommand() : Command("zscore", Arity::exactly(2)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const std::optional<double> score = db.zscore(args.text(0), args.text(1));
        return score ? Reply::bulk(replies::format_score(*score)) : Reply::null();
    }
};

/// ZRANK key member: 0-based position in ascending score order.
class ZRankCommand final : public Command {
public:
    ZRankCommand() : Command("zrank", Arity::exactly(2)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const std::optional<std::size_t> rank = db.zrank(args.text(0), args.text(1));
        return rank ? replies::count(*rank) : Reply::null();
    }
};

/// ZCARD key: number of members.
class ZCardCommand final : public Command {
public:
    ZCardCommand() : Command("zcard", Arity::exactly(1)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        return replies::count(db.zcard(args.text(0)));
    }
};

/// ZRANGE key start stop [WITHSCORES]: members by position; negative indices count from the end.
class ZRangeCommand final : public Command {
public:
    ZRangeCommand() : Command("zrange", Arity::at_least(3)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const bool with_scores = parse_with_scores(args, 3);
        return replies::scored_members(db.zrange(args.text(0), args.integer(1), args.integer(2)),
                                       with_scores);
    }
};

/// ZRANGEBYSCORE key min max [WITHSCORES]: members whose score lies between min and max.
class ZRangeByScoreCommand final : public Command {
public:
    ZRangeByScoreCommand() : Command("zrangebyscore", Arity::at_least(3)) {}

    Reply execute(Database& db, const Arguments& args) const override {
        const bool with_scores = parse_with_scores(args, 3);
        return replies::scored_members(
            db.zrange_by_score(args.text(0), args.score_bound(1), args.score_bound(2)),
            with_scores);
    }
};

}  // namespace

void register_sorted_set_commands(CommandRegistry& registry) {
    registry.add<ZAddCommand>();
    registry.add<ZRemCommand>();
    registry.add<ZScoreCommand>();
    registry.add<ZRankCommand>();
    registry.add<ZCardCommand>();
    registry.add<ZRangeCommand>();
    registry.add<ZRangeByScoreCommand>();
}

}  // namespace memkv::command
