#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

#include "memkv/command/arguments.hpp"
#include "memkv/protocol/reply.hpp"
#include "memkv/storage/database.hpp"

namespace memkv::command {

/// How many arguments (not counting the command name) a command accepts.
class Arity {
public:
    static constexpr Arity exactly(std::size_t count) { return Arity(count, count); }
    static constexpr Arity at_least(std::size_t count) { return Arity(count, kUnlimited); }
    static constexpr Arity between(std::size_t min, std::size_t max) { return Arity(min, max); }

    [[nodiscard]] constexpr bool accepts(std::size_t count) const {
        return count >= min_ && count <= max_;
    }

private:
    static constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();

    constexpr Arity(std::size_t min, std::size_t max) : min_(min), max_(max) {}

    std::size_t min_;
    std::size_t max_;
};

/// One server command, such as GET or ZADD (the Command design pattern).
///
/// A command declares its name and arity, and implements execute(). Commands hold no state:
/// the Database they act on is passed in, so a single instance serves every request.
///
/// Adding a command means writing one subclass and registering it; nothing else changes.
class Command {
public:
    Command(std::string_view name, Arity arity) : name_(name), arity_(arity) {}
    virtual ~Command() = default;

    // Commands are used through pointers to this base class; copying would slice them.
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    /// Lower-case name, e.g. "zadd".
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Arity arity() const noexcept { return arity_; }

    /// Runs the command. May throw CommandError (bad arguments) or storage::WrongTypeError;
    /// the Dispatcher turns both into error replies.
    [[nodiscard]] virtual protocol::Reply execute(storage::Database& db,
                                                  const Arguments& args) const = 0;

private:
    std::string name_;
    Arity arity_;
};

}  // namespace memkv::command
