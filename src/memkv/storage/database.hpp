#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "memkv/common/clock.hpp"
#include "memkv/storage/keyspace.hpp"
#include "memkv/storage/sorted_set.hpp"

namespace memkv::storage {

/// Thrown when an operation for one data type targets a key holding another.
class WrongTypeError : public std::runtime_error {
public:
    WrongTypeError()
        : std::runtime_error("operation against a key holding the wrong kind of value") {}
};

enum class ValueType { kNone, kString, kSortedSet };

enum class SetCondition {
    kAlways,
    kIfAbsent,   // NX
    kIfPresent,  // XX
};

struct SetOptions {
    std::optional<Duration> ttl;
    SetCondition condition = SetCondition::kAlways;
};

struct TimeToLive {
    enum class State { kMissing, kPersistent, kExpiring };

    State state = State::kMissing;
    Duration remaining{0};  // meaningful only when state == kExpiring
};

struct DatabaseStats {
    std::size_t keys = 0;
    std::size_t keys_with_expiry = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t expired_keys = 0;
    std::uint64_t evicted_keys = 0;
};

/// The storage engine's public API: typed operations over the keyspace.
///
/// Knows nothing about sockets or the wire protocol. Read operations count keyspace hits and
/// misses; applying an operation to a key that holds a different type throws WrongTypeError.
class Database {
public:
    /// TTLs above this are rejected so that deadlines can never overflow the clock's range.
    static constexpr Duration kMaxTtl =
        std::chrono::duration_cast<Duration>(std::chrono::years(100));

    explicit Database(const Clock& clock, KeyspaceConfig config = {});

    // ----- Keys -------------------------------------------------------------------------------
    bool del(std::string_view key);
    [[nodiscard]] bool exists(std::string_view key);
    [[nodiscard]] ValueType type(std::string_view key);
    /// Sets a TTL; a non-positive TTL deletes the key immediately. False if the key is missing.
    bool expire(std::string_view key, Duration ttl);
    /// Removes a TTL. False if the key is missing or has no TTL.
    bool persist(std::string_view key);
    [[nodiscard]] TimeToLive ttl(std::string_view key);
    [[nodiscard]] std::size_t size() const noexcept { return keyspace_.size(); }
    void flush() noexcept { keyspace_.clear(); }

    /// Runs one bounded round of active expiration; returns the number of keys removed.
    std::size_t active_expire(std::size_t max_checks) { return keyspace_.expire_due(max_checks); }

    [[nodiscard]] DatabaseStats stats() const noexcept;

    // ----- Strings ----------------------------------------------------------------------------
    [[nodiscard]] std::optional<std::string> get(std::string_view key);
    /// Returns false if the NX/XX condition prevented the write.
    bool set(std::string_view key, std::string value, const SetOptions& options = {});

    // ----- Sorted sets ------------------------------------------------------------------------
    /// Adds members or updates their scores. Returns the number of newly added members.
    std::size_t zadd(std::string_view key, std::span<const ScoredMember> members);
    /// Returns the number of members removed. An emptied sorted set is deleted.
    std::size_t zrem(std::string_view key, std::span<const std::string_view> members);
    [[nodiscard]] std::optional<double> zscore(std::string_view key, std::string_view member);
    [[nodiscard]] std::optional<std::size_t> zrank(std::string_view key, std::string_view member);
    [[nodiscard]] std::size_t zcard(std::string_view key);
    [[nodiscard]] std::vector<ScoredMember> zrange(std::string_view key, std::int64_t start,
                                                   std::int64_t stop);
    [[nodiscard]] std::vector<ScoredMember> zrange_by_score(std::string_view key, ScoreBound min,
                                                            ScoreBound max);

private:
    Entry* lookup_for_read(std::string_view key);
    const SortedSet* find_sorted_set_for_read(std::string_view key);

    Keyspace keyspace_;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};

}  // namespace memkv::storage
