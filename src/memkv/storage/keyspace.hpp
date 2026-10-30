#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "memkv/common/clock.hpp"
#include "memkv/ds/expiry_heap.hpp"
#include "memkv/ds/intrusive_list.hpp"
#include "memkv/storage/sorted_set.hpp"

namespace memkv::storage {

/// The value stored under a key. Supporting a new data type means adding an alternative here.
using Value = std::variant<std::string, SortedSet>;

/// A key together with its value and optional expiration deadline. Entries are threaded on the
/// keyspace's LRU list through the embedded ListHook.
class Entry : public ds::ListHook {
public:
    Entry(std::string key, Value value) : key_(std::move(key)), value_(std::move(value)) {}

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] Value& value() noexcept { return value_; }
    [[nodiscard]] const Value& value() const noexcept { return value_; }
    [[nodiscard]] std::optional<TimePoint> expires_at() const noexcept { return expires_at_; }

private:
    friend class Keyspace;

    const std::string key_;  // immutable: the keyspace index holds views into it
    Value value_;
    std::optional<TimePoint> expires_at_;
    std::uint64_t expiry_ticket_ = 0;  // identifies this entry's live ExpiryHeap item
};

struct KeyspaceConfig {
    /// Upper bound on the number of keys; adding a key beyond it evicts the least recently
    /// used key. 0 means unbounded.
    std::size_t max_keys = 0;
};

struct KeyspaceStats {
    std::uint64_t expired_keys = 0;
    std::uint64_t evicted_keys = 0;
};

/// Owns every key and enforces the policies that apply regardless of value type.
///
///  * Lookup: a hash index (string_view -> Entry) gives O(1) average access. Index keys are views
///    into the entries' own key strings, so lookups by string_view never allocate.
///  * Eviction: every entry is also linked into an intrusive doubly linked list ordered from most
///    to least recently used. Touching a key and evicting the LRU key are both O(1).
///  * Expiration: expired keys are removed lazily when accessed, and actively by expire_due(),
///    which pops deadlines from a min-heap in O(log E) per key. A per-call budget bounds the work,
///    so a burst of simultaneous expirations cannot stall the event loop.
class Keyspace {
public:
    explicit Keyspace(const Clock& clock, KeyspaceConfig config = {});

    /// Returns the live entry for `key` and marks it most recently used, or nullptr if the key is
    /// absent or expired.
    [[nodiscard]] Entry* find(std::string_view key);

    /// Like find(), but leaves the LRU order untouched.
    [[nodiscard]] Entry* peek(std::string_view key);

    /// Stores `value` under `key`, replacing any existing value and clearing its TTL. The entry
    /// becomes the most recently used; inserting a new key may evict the least recently used one.
    Entry& put(std::string_view key, Value value);

    /// Removes `key`. Returns false if it was absent or had already expired.
    bool erase(std::string_view key);

    void set_expiry(Entry& entry, TimePoint deadline);
    void clear_expiry(Entry& entry);

    /// Active expiration: removes keys whose deadline has passed, examining at most `max_checks`
    /// heap items. Returns the number of keys removed.
    std::size_t expire_due(std::size_t max_checks);

    /// Removes every key (statistics are kept).
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t keys_with_expiry() const noexcept { return keys_with_expiry_; }
    [[nodiscard]] std::size_t expiry_heap_size() const noexcept { return expiry_heap_.size(); }
    [[nodiscard]] const KeyspaceStats& stats() const noexcept { return stats_; }
    [[nodiscard]] TimePoint now() const { return clock_.now(); }

    /// Keys ordered from most to least recently used. O(N); intended for tests and debugging.
    [[nodiscard]] std::vector<std::string> keys_by_recency() const;

private:
    using Index = std::unordered_map<std::string_view, std::unique_ptr<Entry>>;

    Entry* lookup(std::string_view key);
    void remove(Index::iterator it);
    void evict_until_below_capacity();
    void compact_expiry_heap_if_needed();
    [[nodiscard]] bool has_expired(const Entry& entry) const;
    [[nodiscard]] bool is_current(const ds::ExpiryHeap::Item& item) const;

    const Clock& clock_;
    KeyspaceConfig config_;
    ds::IntrusiveList<Entry> lru_;  // front = most recently used
    Index index_;
    ds::ExpiryHeap expiry_heap_;
    std::uint64_t next_expiry_ticket_ = 1;
    std::size_t keys_with_expiry_ = 0;
    KeyspaceStats stats_;
};

}  // namespace memkv::storage
