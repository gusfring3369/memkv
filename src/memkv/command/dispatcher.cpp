#include "memkv/command/dispatcher.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace memkv::command {
namespace {

using protocol::Reply;
using storage::Database;
using storage::ScoreBound;
using storage::ScoredMember;
using Args = std::span<const std::string_view>;
using Handler = Reply (*)(Database&, Args);

constexpr std::string_view kWrongTypeMessage =
    "WRONGTYPE Operation against a key holding the wrong kind of value";
constexpr std::string_view kNotAFloat = "value is not a valid float";
constexpr std::string_view kBoundNotAFloat = "min or max is not a float";
constexpr std::size_t kMaxEchoedNameLength = 128;

/// Invalid arguments; reported to the client as "ERR <message>".
class CommandError : public std::runtime_error {
public:
    explicit CommandError(std::string_view message) : std::runtime_error(std::string(message)) {}
};

struct CommandSpec {
    std::string_view name;  // lowercase
    /// Redis convention: N means exactly N arguments including the name, -N means at least N.
    int arity;
    Handler handler;
};

// ----- Argument parsing ------------------------------------------------------------------------

/// ASCII-only case folding: command names and options are ASCII, and this avoids the
/// locale-dependent std::tolower on the hot path.
char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string to_lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), ascii_lower);
    return result;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b,
                              [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

std::int64_t parse_integer(std::string_view text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size()) {
        throw CommandError("value is not an integer or out of range");
    }
    return value;
}

/// Accepts decimal and exponent notation plus "inf", "+inf" and "-inf"; rejects NaN.
double parse_double(std::string_view argument, std::string_view error) {
    if (argument.empty() || std::isspace(static_cast<unsigned char>(argument.front())) != 0) {
        throw CommandError(error);
    }
    const std::string text(argument);  // strtod needs a NUL-terminated string
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);  // the server never changes locale
    const bool overflow = errno == ERANGE && std::isinf(value);
    if (end != text.c_str() + text.size() || std::isnan(value) || overflow) {
        throw CommandError(error);
    }
    return value;
}

/// "1.5" is inclusive, "(1.5" exclusive, as in Redis' ZRANGEBYSCORE.
ScoreBound parse_score_bound(std::string_view text) {
    if (text.starts_with('(')) {
        return {parse_double(text.substr(1), kBoundNotAFloat), true};
    }
    return {parse_double(text, kBoundNotAFloat), false};
}

Duration parse_ttl(std::string_view text, Duration unit, std::string_view command) {
    const std::int64_t amount = parse_integer(text);
    const std::int64_t limit = Database::kMaxTtl / unit;
    if (amount > limit || amount < -limit) {
        throw CommandError("invalid expire time in '" + std::string(command) + "' command");
    }
    return unit * amount;
}

/// Parses an optional trailing WITHSCORES flag located at `position`.
bool parse_with_scores(Args args, std::size_t position) {
    if (args.size() == position) {
        return false;
    }
    if (args.size() == position + 1 && equals_ignore_case(args[position], "withscores")) {
        return true;
    }
    throw CommandError("syntax error");
}

// ----- Reply helpers ---------------------------------------------------------------------------

Reply count_reply(std::size_t count) {
    return Reply::integer(static_cast<std::int64_t>(count));
}

Reply bool_reply(bool value) {
    return Reply::integer(value ? 1 : 0);
}

/// Shortest representation that parses back to the same double, e.g. "1250.5", "inf".
std::string format_score(double score) {
    std::array<char, 32> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), score);
    return {buffer.data(), end};
}

Reply scored_members_reply(std::vector<ScoredMember> members, bool with_scores) {
    Reply::Array elements;
    elements.reserve(members.size() * (with_scores ? 2 : 1));
    for (auto& [member, score] : members) {
        elements.push_back(Reply::bulk(std::move(member)));
        if (with_scores) {
            elements.push_back(Reply::bulk(format_score(score)));
        }
    }
    return Reply::array(std::move(elements));
}

Reply ttl_reply(const storage::TimeToLive& ttl, bool in_seconds) {
    using State = storage::TimeToLive::State;
    switch (ttl.state) {
        case State::kMissing:
            return Reply::integer(-2);
        case State::kPersistent:
            return Reply::integer(-1);
        case State::kExpiring:
            break;
    }
    const std::int64_t millis = ttl.remaining.count();
    return Reply::integer(in_seconds ? (millis + 500) / 1000 : millis);
}

// ----- Server commands -------------------------------------------------------------------------

Reply cmd_ping(Database& /*db*/, Args args) {
    if (args.size() > 2) {
        throw CommandError("wrong number of arguments for 'ping' command");
    }
    return args.size() == 1 ? Reply::simple("PONG") : Reply::bulk(std::string(args[1]));
}

Reply cmd_echo(Database& /*db*/, Args args) {
    return Reply::bulk(std::string(args[1]));
}

Reply cmd_dbsize(Database& db, Args /*args*/) {
    return count_reply(db.size());
}

Reply cmd_flushall(Database& db, Args /*args*/) {
    db.flush();
    return Reply::ok();
}

Reply cmd_info(Database& db, Args /*args*/) {
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

/// Command introspection is not implemented; an empty reply keeps redis-cli and
/// redis-benchmark happy when they probe for it on connect.
Reply cmd_empty_array(Database& /*db*/, Args /*args*/) {
    return Reply::array({});
}

// ----- Key commands ----------------------------------------------------------------------------

Reply cmd_del(Database& db, Args args) {
    std::size_t deleted = 0;
    for (const std::string_view key : args.subspan(1)) {
        if (db.del(key)) {
            ++deleted;
        }
    }
    return count_reply(deleted);
}

Reply cmd_exists(Database& db, Args args) {
    std::size_t found = 0;
    for (const std::string_view key : args.subspan(1)) {
        if (db.exists(key)) {
            ++found;
        }
    }
    return count_reply(found);
}

Reply cmd_type(Database& db, Args args) {
    switch (db.type(args[1])) {
        case storage::ValueType::kString:
            return Reply::simple("string");
        case storage::ValueType::kSortedSet:
            return Reply::simple("zset");
        case storage::ValueType::kNone:
            break;
    }
    return Reply::simple("none");
}

Reply cmd_expire(Database& db, Args args) {
    return bool_reply(db.expire(args[1], parse_ttl(args[2], std::chrono::seconds(1), "expire")));
}

Reply cmd_pexpire(Database& db, Args args) {
    return bool_reply(
        db.expire(args[1], parse_ttl(args[2], std::chrono::milliseconds(1), "pexpire")));
}

Reply cmd_ttl(Database& db, Args args) {
    return ttl_reply(db.ttl(args[1]), true);
}

Reply cmd_pttl(Database& db, Args args) {
    return ttl_reply(db.ttl(args[1]), false);
}

Reply cmd_persist(Database& db, Args args) {
    return bool_reply(db.persist(args[1]));
}

// ----- String commands -------------------------------------------------------------------------

Reply cmd_get(Database& db, Args args) {
    std::optional<std::string> value = db.get(args[1]);
    return value ? Reply::bulk(std::move(*value)) : Reply::null();
}

/// SET key value [NX | XX] [EX seconds | PX milliseconds]
Reply cmd_set(Database& db, Args args) {
    using storage::SetCondition;
    storage::SetOptions options;
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string option = to_lower(args[i]);
        if (option == "nx" || option == "xx") {
            const SetCondition condition =
                option == "nx" ? SetCondition::kIfAbsent : SetCondition::kIfPresent;
            if (options.condition != SetCondition::kAlways && options.condition != condition) {
                throw CommandError("syntax error");
            }
            options.condition = condition;
        } else if ((option == "ex" || option == "px") && i + 1 < args.size() && !options.ttl) {
            const Duration unit =
                option == "ex" ? Duration(std::chrono::seconds(1)) : std::chrono::milliseconds(1);
            const Duration ttl = parse_ttl(args[++i], unit, "set");
            if (ttl <= Duration::zero()) {
                throw CommandError("invalid expire time in 'set' command");
            }
            options.ttl = ttl;
        } else {
            throw CommandError("syntax error");
        }
    }
    return db.set(args[1], std::string(args[2]), options) ? Reply::ok() : Reply::null();
}

// ----- Sorted set commands ---------------------------------------------------------------------

/// ZADD key score member [score member ...]
Reply cmd_zadd(Database& db, Args args) {
    if (args.size() % 2 != 0) {
        throw CommandError("syntax error");
    }
    // Validate every score before touching the set so a bad argument changes nothing.
    std::vector<ScoredMember> members;
    members.reserve((args.size() - 2) / 2);
    for (std::size_t i = 2; i < args.size(); i += 2) {
        members.push_back({std::string(args[i + 1]), parse_double(args[i], kNotAFloat)});
    }
    return count_reply(db.zadd(args[1], members));
}

Reply cmd_zrem(Database& db, Args args) {
    return count_reply(db.zrem(args[1], args.subspan(2)));
}

Reply cmd_zscore(Database& db, Args args) {
    const std::optional<double> score = db.zscore(args[1], args[2]);
    return score ? Reply::bulk(format_score(*score)) : Reply::null();
}

Reply cmd_zrank(Database& db, Args args) {
    const std::optional<std::size_t> rank = db.zrank(args[1], args[2]);
    return rank ? count_reply(*rank) : Reply::null();
}

Reply cmd_zcard(Database& db, Args args) {
    return count_reply(db.zcard(args[1]));
}

/// ZRANGE key start stop [WITHSCORES]
Reply cmd_zrange(Database& db, Args args) {
    const bool with_scores = parse_with_scores(args, 4);
    return scored_members_reply(db.zrange(args[1], parse_integer(args[2]), parse_integer(args[3])),
                                with_scores);
}

/// ZRANGEBYSCORE key min max [WITHSCORES]
Reply cmd_zrangebyscore(Database& db, Args args) {
    const bool with_scores = parse_with_scores(args, 4);
    return scored_members_reply(
        db.zrange_by_score(args[1], parse_score_bound(args[2]), parse_score_bound(args[3])),
        with_scores);
}

// ----- Command table ---------------------------------------------------------------------------

constexpr std::array kCommands{
    CommandSpec{"ping", -1, cmd_ping},
    CommandSpec{"echo", 2, cmd_echo},
    CommandSpec{"dbsize", 1, cmd_dbsize},
    CommandSpec{"flushall", -1, cmd_flushall},
    CommandSpec{"flushdb", -1, cmd_flushall},
    CommandSpec{"info", -1, cmd_info},
    CommandSpec{"command", -1, cmd_empty_array},
    CommandSpec{"config", -2, cmd_empty_array},
    CommandSpec{"del", -2, cmd_del},
    CommandSpec{"exists", -2, cmd_exists},
    CommandSpec{"type", 2, cmd_type},
    CommandSpec{"expire", 3, cmd_expire},
    CommandSpec{"pexpire", 3, cmd_pexpire},
    CommandSpec{"ttl", 2, cmd_ttl},
    CommandSpec{"pttl", 2, cmd_pttl},
    CommandSpec{"persist", 2, cmd_persist},
    CommandSpec{"get", 2, cmd_get},
    CommandSpec{"set", -3, cmd_set},
    CommandSpec{"zadd", -4, cmd_zadd},
    CommandSpec{"zrem", -3, cmd_zrem},
    CommandSpec{"zscore", 3, cmd_zscore},
    CommandSpec{"zrank", 3, cmd_zrank},
    CommandSpec{"zcard", 2, cmd_zcard},
    CommandSpec{"zrange", -4, cmd_zrange},
    CommandSpec{"zrangebyscore", -4, cmd_zrangebyscore},
};

const CommandSpec* find_command(std::string_view name) {
    static const std::unordered_map<std::string_view, const CommandSpec*> table = [] {
        std::unordered_map<std::string_view, const CommandSpec*> map;
        for (const CommandSpec& spec : kCommands) {
            map.emplace(spec.name, &spec);
        }
        return map;
    }();
    const auto it = table.find(to_lower(name));
    return it == table.end() ? nullptr : it->second;
}

bool arity_matches(int arity, std::size_t argc) {
    return arity >= 0 ? argc == static_cast<std::size_t>(arity)
                      : argc >= static_cast<std::size_t>(-arity);
}

}  // namespace

Reply Dispatcher::dispatch(std::span<const std::string_view> argv) {
    if (argv.empty()) {
        return Reply::error("ERR empty command");
    }
    const CommandSpec* spec = find_command(argv[0]);
    if (spec == nullptr) {
        return Reply::error("ERR unknown command '" +
                            std::string(argv[0].substr(0, kMaxEchoedNameLength)) + "'");
    }
    if (!arity_matches(spec->arity, argv.size())) {
        return Reply::error("ERR wrong number of arguments for '" + std::string(spec->name) +
                            "' command");
    }
    try {
        return spec->handler(db_, argv);
    } catch (const storage::WrongTypeError&) {
        return Reply::error(std::string(kWrongTypeMessage));
    } catch (const CommandError& e) {
        return Reply::error(std::string("ERR ") + e.what());
    }
}

}  // namespace memkv::command
