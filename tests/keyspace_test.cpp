#include "memkv/storage/keyspace.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "support/manual_clock.hpp"

namespace memkv::storage {
namespace {

using namespace std::chrono_literals;
using Keys = std::vector<std::string>;

class KeyspaceTest : public ::testing::Test {
protected:
    Entry& put(Keyspace& keyspace, const std::string& key) { return keyspace.put(key, key); }

    testing::ManualClock clock_;
};

TEST_F(KeyspaceTest, PutAndFind) {
    Keyspace keyspace(clock_);
    keyspace.put("k", std::string("v"));
    Entry* entry = keyspace.find("k");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(std::get<std::string>(entry->value()), "v");
    EXPECT_EQ(keyspace.find("missing"), nullptr);
}

TEST_F(KeyspaceTest, FindPromotesToMostRecentlyUsed) {
    Keyspace keyspace(clock_);
    put(keyspace, "a");
    put(keyspace, "b");
    put(keyspace, "c");
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"c", "b", "a"}));

    (void)keyspace.find("a");
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"a", "c", "b"}));

    (void)keyspace.peek("b");  // peek must not reorder
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"a", "c", "b"}));
}

TEST_F(KeyspaceTest, EvictsLeastRecentlyUsedAtCapacity) {
    Keyspace keyspace(clock_, {.max_keys = 3});
    put(keyspace, "user:101");
    put(keyspace, "user:102");
    put(keyspace, "user:103");
    (void)keyspace.find("user:101");

    put(keyspace, "user:104");
    EXPECT_EQ(keyspace.size(), 3U);
    EXPECT_EQ(keyspace.peek("user:102"), nullptr);
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"user:104", "user:101", "user:103"}));
    EXPECT_EQ(keyspace.stats().evicted_keys, 1U);
}

TEST_F(KeyspaceTest, OverwritingExistingKeyDoesNotEvict) {
    Keyspace keyspace(clock_, {.max_keys = 2});
    put(keyspace, "a");
    put(keyspace, "b");
    put(keyspace, "a");
    EXPECT_EQ(keyspace.size(), 2U);
    EXPECT_EQ(keyspace.stats().evicted_keys, 0U);
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"a", "b"}));
}

TEST_F(KeyspaceTest, LazilyExpiresOnAccess) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "otp"), clock_.now() + 100ms);

    clock_.advance(99ms);
    EXPECT_NE(keyspace.find("otp"), nullptr);
    clock_.advance(1ms);
    EXPECT_EQ(keyspace.find("otp"), nullptr);
    EXPECT_EQ(keyspace.size(), 0U);
    EXPECT_EQ(keyspace.stats().expired_keys, 1U);
}

TEST_F(KeyspaceTest, ActiveExpirationRemovesDueKeysOnly) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "soon"), clock_.now() + 10ms);
    keyspace.set_expiry(put(keyspace, "later"), clock_.now() + 1h);
    put(keyspace, "forever");

    clock_.advance(10ms);
    EXPECT_EQ(keyspace.expire_due(100), 1U);
    EXPECT_EQ(keyspace.size(), 2U);
    EXPECT_EQ(keyspace.peek("soon"), nullptr);
    EXPECT_EQ(keyspace.keys_with_expiry(), 1U);
}

TEST_F(KeyspaceTest, ActiveExpirationRespectsBudget) {
    Keyspace keyspace(clock_);
    for (int i = 0; i < 10; ++i) {
        keyspace.set_expiry(put(keyspace, "k" + std::to_string(i)), clock_.now() + 1ms);
    }
    clock_.advance(1ms);
    EXPECT_EQ(keyspace.expire_due(4), 4U);
    EXPECT_EQ(keyspace.size(), 6U);
    EXPECT_EQ(keyspace.expire_due(100), 6U);
}

TEST_F(KeyspaceTest, ExtendedTtlIsNotExpiredByStaleHeapItem) {
    Keyspace keyspace(clock_);
    Entry& entry = put(keyspace, "session");
    keyspace.set_expiry(entry, clock_.now() + 60s);
    clock_.advance(5s);
    keyspace.set_expiry(entry, clock_.now() + 90s);  // the 60s heap item is now stale

    clock_.advance(60s);
    EXPECT_EQ(keyspace.expire_due(100), 0U);
    EXPECT_NE(keyspace.peek("session"), nullptr);

    clock_.advance(30s);
    EXPECT_EQ(keyspace.expire_due(100), 1U);
}

TEST_F(KeyspaceTest, OverwriteClearsTtl) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "k"), clock_.now() + 1s);
    put(keyspace, "k");
    clock_.advance(2s);
    EXPECT_EQ(keyspace.expire_due(100), 0U);
    EXPECT_NE(keyspace.find("k"), nullptr);
    EXPECT_EQ(keyspace.keys_with_expiry(), 0U);
}

TEST_F(KeyspaceTest, RecreatedKeyIsNotExpiredByPredecessorsHeapItem) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "k"), clock_.now() + 1s);
    EXPECT_TRUE(keyspace.erase("k"));
    keyspace.set_expiry(put(keyspace, "k"), clock_.now() + 1h);

    clock_.advance(2s);
    EXPECT_EQ(keyspace.expire_due(100), 0U);
    EXPECT_NE(keyspace.peek("k"), nullptr);
}

TEST_F(KeyspaceTest, EraseOfExpiredKeyReportsAbsent) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "k"), clock_.now() + 1ms);
    clock_.advance(1ms);
    EXPECT_FALSE(keyspace.erase("k"));
    EXPECT_EQ(keyspace.size(), 0U);
}

TEST_F(KeyspaceTest, ExpiryHeapStaysProportionalToKeysWithTtl) {
    Keyspace keyspace(clock_);
    Entry& entry = put(keyspace, "hot");
    // Refreshing the same TTL repeatedly leaves stale heap items behind; compaction bounds them.
    for (int i = 0; i < 100'000; ++i) {
        keyspace.set_expiry(entry, clock_.now() + std::chrono::seconds(i + 1));
    }
    EXPECT_LE(keyspace.expiry_heap_size(), 1024U);
    EXPECT_EQ(keyspace.keys_with_expiry(), 1U);
}

TEST_F(KeyspaceTest, ClearRemovesEverything) {
    Keyspace keyspace(clock_);
    keyspace.set_expiry(put(keyspace, "a"), clock_.now() + 1s);
    put(keyspace, "b");
    keyspace.clear();
    EXPECT_EQ(keyspace.size(), 0U);
    EXPECT_EQ(keyspace.keys_with_expiry(), 0U);
    EXPECT_TRUE(keyspace.keys_by_recency().empty());
    put(keyspace, "c");  // still usable afterwards
    EXPECT_EQ(keyspace.keys_by_recency(), (Keys{"c"}));
}

}  // namespace
}  // namespace memkv::storage
