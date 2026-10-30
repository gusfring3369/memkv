// Tests for the command-layer building blocks: Arity, Arguments and CommandRegistry.

#include "memkv/command/command.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "memkv/command/arguments.hpp"
#include "memkv/command/command_error.hpp"
#include "memkv/command/command_registry.hpp"

namespace memkv::command {
namespace {

TEST(ArityTest, ExactlyAtLeastAndBetween) {
    EXPECT_TRUE(Arity::exactly(1).accepts(1));
    EXPECT_FALSE(Arity::exactly(1).accepts(2));
    EXPECT_FALSE(Arity::at_least(2).accepts(1));
    EXPECT_TRUE(Arity::at_least(2).accepts(1000));
    EXPECT_TRUE(Arity::between(0, 1).accepts(0));
    EXPECT_FALSE(Arity::between(0, 1).accepts(2));
}

TEST(ArgumentsTest, ConvertsValidArguments) {
    const std::vector<std::string_view> values = {"key", "-42", "1.5", "(2", "+inf", "WithScores"};
    const Arguments args(values);
    EXPECT_EQ(args.size(), 6U);
    EXPECT_EQ(args.text(0), "key");
    EXPECT_EQ(args.integer(1), -42);
    EXPECT_EQ(args.score(2), 1.5);
    EXPECT_EQ(args.score_bound(3).value, 2);
    EXPECT_TRUE(args.score_bound(3).exclusive);
    EXPECT_TRUE(std::isinf(args.score(4)));
    EXPECT_TRUE(args.is(5, "withscores"));
    EXPECT_EQ(args.from(4).size(), 2U);
}

TEST(ArgumentsTest, RejectsMalformedArguments) {
    const std::vector<std::string_view> values = {"12abc", "nan", " 1", "", "99999999999999999999"};
    const Arguments args(values);
    EXPECT_THROW((void)args.integer(0), CommandError);
    EXPECT_THROW((void)args.score(1), CommandError);
    EXPECT_THROW((void)args.score(2), CommandError);
    EXPECT_THROW((void)args.integer(3), CommandError);
    EXPECT_THROW((void)args.integer(4), CommandError);
}

TEST(ArgumentsTest, TtlRespectsUnitAndLimit) {
    const std::vector<std::string_view> values = {"10", "999999999999999"};
    const Arguments args(values);
    EXPECT_EQ(args.ttl(0, std::chrono::seconds(1), "expire"), std::chrono::seconds(10));
    EXPECT_THROW((void)args.ttl(1, std::chrono::seconds(1), "expire"), CommandError);
}

class FakeCommand final : public Command {
public:
    FakeCommand() : Command("Fake", Arity::exactly(0)) {}
    protocol::Reply execute(storage::Database& /*db*/, const Arguments& /*args*/) const override {
        return protocol::Reply::ok();
    }
};

TEST(CommandRegistryTest, FindsCommandsIgnoringCase) {
    CommandRegistry registry;
    registry.add<FakeCommand>();
    EXPECT_NE(registry.find("fake"), nullptr);
    EXPECT_NE(registry.find("FAKE"), nullptr);
    EXPECT_EQ(registry.find("other"), nullptr);
}

TEST(CommandRegistryTest, RejectsDuplicateNames) {
    CommandRegistry registry;
    registry.add<FakeCommand>();
    EXPECT_THROW(registry.add<FakeCommand>(), std::logic_error);
}

TEST(CommandRegistryTest, BuiltinRegistryHasEveryCommand) {
    const CommandRegistry registry = CommandRegistry::with_builtin_commands();
    for (const std::string_view name :
         {"ping",   "echo",    "dbsize", "flushall",     "flushdb", "info",    "command",
          "config", "del",     "exists", "type",         "expire",  "pexpire", "ttl",
          "pttl",   "persist", "get",    "set",          "zadd",    "zrem",    "zscore",
          "zrank",  "zcard",   "zrange", "zrangebyscore"}) {
        EXPECT_NE(registry.find(name), nullptr) << name;
    }
    EXPECT_EQ(registry.size(), 25U);
}

}  // namespace
}  // namespace memkv::command
