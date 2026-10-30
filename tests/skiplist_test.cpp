#include "memkv/ds/skiplist.hpp"

#include <gtest/gtest.h>

#include <iterator>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace memkv::ds {
namespace {

std::vector<std::string> members_of(const std::vector<ScoredMember>& elements) {
    std::vector<std::string> members;
    for (const auto& element : elements) {
        members.push_back(element.member);
    }
    return members;
}

TEST(SkipListTest, OrdersByScoreThenMember) {
    SkipList list(42);
    list.insert(2.0, "b");
    list.insert(1.0, "z");
    list.insert(2.0, "a");
    list.insert(-5.0, "neg");

    EXPECT_EQ(list.size(), 4U);
    EXPECT_EQ(members_of(list.range_by_rank(0, 10)),
              (std::vector<std::string>{"neg", "z", "a", "b"}));
}

TEST(SkipListTest, RankIsZeroBasedAndMissingIsNullopt) {
    SkipList list(1);
    list.insert(10, "x");
    list.insert(20, "y");
    list.insert(30, "z");

    EXPECT_EQ(list.rank(10, "x"), 0U);
    EXPECT_EQ(list.rank(30, "z"), 2U);
    EXPECT_EQ(list.rank(20, "nope"), std::nullopt);
    EXPECT_EQ(list.rank(25, "y"), std::nullopt);  // right member, wrong score
}

TEST(SkipListTest, EraseRemovesOnlyExactPair) {
    SkipList list(7);
    list.insert(1, "a");
    list.insert(2, "b");

    EXPECT_FALSE(list.erase(1, "b"));
    EXPECT_TRUE(list.erase(1, "a"));
    EXPECT_FALSE(list.erase(1, "a"));
    EXPECT_EQ(list.size(), 1U);
    EXPECT_EQ(list.rank(2, "b"), 0U);
}

TEST(SkipListTest, UpdateScoreRepositionsAndKeepsMemberStorage) {
    SkipList list(3);
    const std::string& stored = list.insert(1, "a");
    list.insert(2, "b");
    list.insert(3, "c");

    list.update_score(1, "a", 10);
    EXPECT_EQ(stored, "a");  // still valid after the move
    EXPECT_EQ(members_of(list.range_by_rank(0, 2)), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_EQ(list.rank(10, "a"), 2U);
}

TEST(SkipListTest, RangeByScoreHonoursInclusiveAndExclusiveBounds) {
    SkipList list(5);
    for (int i = 1; i <= 5; ++i) {
        list.insert(i, "m" + std::to_string(i));
    }
    EXPECT_EQ(members_of(list.range_by_score({2, false}, {4, false})),
              (std::vector<std::string>{"m2", "m3", "m4"}));
    EXPECT_EQ(members_of(list.range_by_score({2, true}, {4, true})),
              (std::vector<std::string>{"m3"}));
    EXPECT_TRUE(list.range_by_score({4, false}, {2, false}).empty());
    EXPECT_EQ(list.range_by_score({-1e300, false}, {1e300, false}).size(), 5U);
}

TEST(SkipListTest, RangeByRankClampsAndRejectsInvalidRanges) {
    SkipList list(9);
    list.insert(1, "a");
    list.insert(2, "b");
    EXPECT_EQ(list.range_by_rank(1, 100).size(), 1U);
    EXPECT_TRUE(list.range_by_rank(2, 5).empty());
    EXPECT_TRUE(list.range_by_rank(1, 0).empty());
}

TEST(SkipListTest, MoveTransfersOwnership) {
    SkipList source(11);
    source.insert(1, "a");
    SkipList target(std::move(source));
    EXPECT_EQ(target.size(), 1U);

    SkipList other(12);
    other.insert(5, "x");
    other = std::move(target);
    EXPECT_EQ(other.rank(1, "a"), 0U);
}

// Differential test: random operations checked against std::set, the obvious-but-slow oracle.
// Exercises span bookkeeping across many level promotions, demotions and node reuse.
TEST(SkipListTest, MatchesOrderedSetUnderRandomOperations) {
    using Pair = std::pair<double, std::string>;
    std::mt19937 rng(2024);
    std::uniform_int_distribution<int> score_dist(0, 200);
    std::uniform_int_distribution<int> member_dist(0, 499);
    std::uniform_int_distribution<int> op_dist(0, 9);

    SkipList list(99);
    std::set<Pair> oracle;
    std::vector<double> score_of(500, -1);  // -1 = absent

    for (int step = 0; step < 20000; ++step) {
        const int id = member_dist(rng);
        const std::string member = "member:" + std::to_string(id);
        const double score = score_dist(rng);
        const int op = op_dist(rng);

        if (score_of[static_cast<std::size_t>(id)] < 0) {
            list.insert(score, member);
            oracle.emplace(score, member);
            score_of[static_cast<std::size_t>(id)] = score;
        } else if (op < 4) {
            const double old_score = score_of[static_cast<std::size_t>(id)];
            ASSERT_TRUE(list.erase(old_score, member));
            oracle.erase({old_score, member});
            score_of[static_cast<std::size_t>(id)] = -1;
        } else {
            const double old_score = score_of[static_cast<std::size_t>(id)];
            list.update_score(old_score, member, score);
            oracle.erase({old_score, member});
            oracle.emplace(score, member);
            score_of[static_cast<std::size_t>(id)] = score;
        }

        ASSERT_EQ(list.size(), oracle.size());
        if (step % 500 == 0) {
            std::size_t expected_rank = 0;
            for (const auto& [s, m] : oracle) {
                ASSERT_EQ(list.rank(s, m), expected_rank++);
            }
            const auto all = list.range_by_rank(0, oracle.size());
            ASSERT_EQ(all.size(), oracle.size());
            auto it = oracle.begin();
            for (const auto& element : all) {
                ASSERT_EQ(element.score, it->first);
                ASSERT_EQ(element.member, it->second);
                ++it;
            }
        }
    }

    // Rank-window queries must agree with the oracle at arbitrary offsets.
    for (std::size_t first = 0; first < oracle.size(); first += 37) {
        const auto window = list.range_by_rank(first, first + 9);
        auto it = std::next(oracle.begin(), static_cast<std::ptrdiff_t>(first));
        for (const auto& element : window) {
            ASSERT_EQ(element.member, it->second);
            ++it;
        }
    }
}

}  // namespace
}  // namespace memkv::ds
