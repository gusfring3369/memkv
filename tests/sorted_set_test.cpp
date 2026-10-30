#include "memkv/storage/sorted_set.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace memkv::storage {
namespace {

using AddResult = SortedSet::AddResult;

std::vector<std::string> members_of(const std::vector<ScoredMember>& elements) {
    std::vector<std::string> members;
    for (const auto& element : elements) {
        members.push_back(element.member);
    }
    return members;
}

TEST(SortedSetTest, AddReportsAddedUpdatedOrUnchanged) {
    SortedSet set;
    EXPECT_EQ(set.add("alice", 10), AddResult::kAdded);
    EXPECT_EQ(set.add("alice", 10), AddResult::kUnchanged);
    EXPECT_EQ(set.add("alice", 20), AddResult::kUpdated);
    EXPECT_EQ(set.size(), 1U);
    EXPECT_EQ(set.score("alice"), 20);
}

// The original single-structure design inserted a second node when a member's score changed.
TEST(SortedSetTest, UpdatingScoreNeverDuplicatesMember) {
    SortedSet set;
    set.add("alice", 10);
    set.add("bob", 15);
    set.add("alice", 20);

    EXPECT_EQ(members_of(set.range_by_rank(0, -1)), (std::vector<std::string>{"bob", "alice"}));
    EXPECT_EQ(set.rank("alice"), 1U);
}

TEST(SortedSetTest, RemoveDeletesFromBothIndexes) {
    SortedSet set;
    set.add("a", 1);
    set.add("b", 2);
    EXPECT_TRUE(set.remove("a"));
    EXPECT_FALSE(set.remove("a"));
    EXPECT_EQ(set.score("a"), std::nullopt);
    EXPECT_EQ(set.rank("b"), 0U);
    EXPECT_EQ(set.size(), 1U);
}

TEST(SortedSetTest, RangeByRankSupportsNegativeIndices) {
    SortedSet set;
    for (int i = 0; i < 5; ++i) {
        set.add("m" + std::to_string(i), i);
    }
    EXPECT_EQ(members_of(set.range_by_rank(-2, -1)), (std::vector<std::string>{"m3", "m4"}));
    EXPECT_EQ(members_of(set.range_by_rank(0, 1)), (std::vector<std::string>{"m0", "m1"}));
    EXPECT_EQ(set.range_by_rank(-100, 100).size(), 5U);
    EXPECT_TRUE(set.range_by_rank(3, 1).empty());
    EXPECT_TRUE(set.range_by_rank(5, 10).empty());
}

TEST(SortedSetTest, RangeByScoreReturnsScores) {
    SortedSet set;
    set.add("low", 1.5);
    set.add("mid", 2.5);
    set.add("high", 3.5);
    const auto result = set.range_by_score({2, false}, {4, false});
    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0], (ScoredMember{"mid", 2.5}));
    EXPECT_EQ(result[1], (ScoredMember{"high", 3.5}));
}

TEST(SortedSetTest, SurvivesMove) {
    SortedSet source;
    source.add("a", 1);
    SortedSet moved(std::move(source));
    EXPECT_EQ(moved.score("a"), 1);  // index keys still point at live node storage
    EXPECT_EQ(moved.add("a", 2), AddResult::kUpdated);
}

}  // namespace
}  // namespace memkv::storage
