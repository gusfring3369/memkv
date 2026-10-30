#include "memkv/storage/database.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "support/manual_clock.hpp"

namespace memkv::storage {
namespace {

using namespace std::chrono_literals;

class DatabaseTest : public ::testing::Test {
protected:
    testing::ManualClock clock_;
    Database db_{clock_};
};

TEST_F(DatabaseTest, SetGetDel) {
    EXPECT_TRUE(db_.set("k", "v"));
    EXPECT_EQ(db_.get("k"), "v");
    EXPECT_TRUE(db_.del("k"));
    EXPECT_FALSE(db_.del("k"));
    EXPECT_EQ(db_.get("k"), std::nullopt);
}

TEST_F(DatabaseTest, SetConditions) {
    EXPECT_FALSE(db_.set("k", "v", {.condition = SetCondition::kIfPresent}));
    EXPECT_TRUE(db_.set("k", "v1", {.condition = SetCondition::kIfAbsent}));
    EXPECT_FALSE(db_.set("k", "v2", {.condition = SetCondition::kIfAbsent}));
    EXPECT_TRUE(db_.set("k", "v3", {.condition = SetCondition::kIfPresent}));
    EXPECT_EQ(db_.get("k"), "v3");
}

TEST_F(DatabaseTest, SetWithTtlExpires) {
    db_.set("otp", "482109", {.ttl = 100ms});
    EXPECT_EQ(db_.get("otp"), "482109");
    clock_.advance(120ms);
    EXPECT_EQ(db_.get("otp"), std::nullopt);
}

TEST_F(DatabaseTest, TtlReportsState) {
    EXPECT_EQ(db_.ttl("missing").state, TimeToLive::State::kMissing);
    db_.set("k", "v");
    EXPECT_EQ(db_.ttl("k").state, TimeToLive::State::kPersistent);
    EXPECT_TRUE(db_.expire("k", 10s));
    clock_.advance(4s);
    const TimeToLive ttl = db_.ttl("k");
    EXPECT_EQ(ttl.state, TimeToLive::State::kExpiring);
    EXPECT_EQ(ttl.remaining, 6s);
    EXPECT_TRUE(db_.persist("k"));
    EXPECT_FALSE(db_.persist("k"));
    EXPECT_EQ(db_.ttl("k").state, TimeToLive::State::kPersistent);
}

TEST_F(DatabaseTest, NonPositiveExpireDeletesKey) {
    db_.set("k", "v");
    EXPECT_TRUE(db_.expire("k", 0ms));
    EXPECT_FALSE(db_.exists("k"));
    EXPECT_FALSE(db_.expire("k", 10s));
}

TEST_F(DatabaseTest, TypeReflectsStoredValue) {
    db_.set("s", "v");
    const std::vector<ScoredMember> members{{"m", 1}};
    db_.zadd("z", members);
    EXPECT_EQ(db_.type("s"), ValueType::kString);
    EXPECT_EQ(db_.type("z"), ValueType::kSortedSet);
    EXPECT_EQ(db_.type("none"), ValueType::kNone);
}

TEST_F(DatabaseTest, WrongTypeIsRejected) {
    db_.set("s", "v");
    const std::vector<ScoredMember> members{{"m", 1}};
    EXPECT_THROW(db_.zadd("s", members), WrongTypeError);
    EXPECT_THROW((void)db_.zscore("s", "m"), WrongTypeError);

    db_.zadd("z", members);
    EXPECT_THROW((void)db_.get("z"), WrongTypeError);
}

TEST_F(DatabaseTest, SetOverwritesAnyType) {
    const std::vector<ScoredMember> members{{"m", 1}};
    db_.zadd("k", members);
    db_.set("k", "now a string");
    EXPECT_EQ(db_.get("k"), "now a string");
}

TEST_F(DatabaseTest, SortedSetLifecycle) {
    const std::vector<ScoredMember> scores{
        {"Player_One", 1250.5}, {"Player_Two", 890}, {"Pro_Gamer", 2400},
        {"Mid_Tier", 1540},     {"Sniper_99", 1720},
    };
    EXPECT_EQ(db_.zadd("leaderboard", scores), 5U);
    EXPECT_EQ(db_.zcard("leaderboard"), 5U);

    const auto in_range = db_.zrange_by_score("leaderboard", {1200, false}, {1800, false});
    ASSERT_EQ(in_range.size(), 3U);
    EXPECT_EQ(in_range.front(), (ScoredMember{"Player_One", 1250.5}));
    EXPECT_EQ(in_range.back(), (ScoredMember{"Sniper_99", 1720}));

    EXPECT_EQ(db_.zrank("leaderboard", "Pro_Gamer"), 4U);
    EXPECT_EQ(db_.zrange("leaderboard", 0, 0).front().member, "Player_Two");

    const std::vector<ScoredMember> update{{"Player_Two", 5000}};
    EXPECT_EQ(db_.zadd("leaderboard", update), 0U);  // update, not an addition
    EXPECT_EQ(db_.zrank("leaderboard", "Player_Two"), 4U);
}

TEST_F(DatabaseTest, RemovingLastMemberDeletesKey) {
    const std::vector<ScoredMember> members{{"a", 1}, {"b", 2}};
    db_.zadd("z", members);
    const std::vector<std::string_view> to_remove{"a", "b", "c"};
    EXPECT_EQ(db_.zrem("z", to_remove), 2U);
    EXPECT_FALSE(db_.exists("z"));
}

TEST_F(DatabaseTest, CountsHitsAndMisses) {
    db_.set("k", "v");
    (void)db_.get("k");
    (void)db_.get("k");
    (void)db_.get("missing");
    const DatabaseStats stats = db_.stats();
    EXPECT_EQ(stats.hits, 2U);
    EXPECT_EQ(stats.misses, 1U);
    EXPECT_EQ(stats.keys, 1U);
}

}  // namespace
}  // namespace memkv::storage
