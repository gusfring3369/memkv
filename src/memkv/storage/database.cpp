#include "memkv/storage/database.hpp"

#include <algorithm>
#include <chrono>
#include <variant>

namespace memkv::storage {
namespace {

template <typename T>
T& value_as(Entry& entry) {
    if (T* value = std::get_if<T>(&entry.value())) {
        return *value;
    }
    throw WrongTypeError();
}

}  // namespace

Database::Database(const Clock& clock, KeyspaceConfig config) : keyspace_(clock, config) {}

// ----- Keys -----------------------------------------------------------------------------------

bool Database::del(std::string_view key) {
    return keyspace_.erase(key);
}

bool Database::exists(std::string_view key) {
    return keyspace_.peek(key) != nullptr;
}

ValueType Database::type(std::string_view key) {
    const Entry* entry = keyspace_.peek(key);
    if (entry == nullptr) {
        return ValueType::kNone;
    }
    return std::holds_alternative<std::string>(entry->value()) ? ValueType::kString
                                                               : ValueType::kSortedSet;
}

bool Database::expire(std::string_view key, Duration ttl) {
    Entry* entry = keyspace_.peek(key);
    if (entry == nullptr) {
        return false;
    }
    if (ttl <= Duration::zero()) {
        keyspace_.erase(key);
        return true;
    }
    keyspace_.set_expiry(*entry, keyspace_.now() + ttl);
    return true;
}

bool Database::persist(std::string_view key) {
    Entry* entry = keyspace_.peek(key);
    if (entry == nullptr || !entry->expires_at()) {
        return false;
    }
    keyspace_.clear_expiry(*entry);
    return true;
}

TimeToLive Database::ttl(std::string_view key) {
    const Entry* entry = keyspace_.peek(key);
    if (entry == nullptr) {
        return {TimeToLive::State::kMissing};
    }
    const std::optional<TimePoint> deadline = entry->expires_at();
    if (!deadline) {
        return {TimeToLive::State::kPersistent};
    }
    const auto remaining = std::chrono::duration_cast<Duration>(*deadline - keyspace_.now());
    return {TimeToLive::State::kExpiring, std::max(remaining, Duration::zero())};
}

DatabaseStats Database::stats() const noexcept {
    const KeyspaceStats& keyspace_stats = keyspace_.stats();
    return {
        .keys = keyspace_.size(),
        .keys_with_expiry = keyspace_.keys_with_expiry(),
        .hits = hits_,
        .misses = misses_,
        .expired_keys = keyspace_stats.expired_keys,
        .evicted_keys = keyspace_stats.evicted_keys,
    };
}

// ----- Strings --------------------------------------------------------------------------------

std::optional<std::string> Database::get(std::string_view key) {
    Entry* entry = lookup_for_read(key);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return value_as<std::string>(*entry);
}

bool Database::set(std::string_view key, std::string value, const SetOptions& options) {
    if (options.condition != SetCondition::kAlways) {
        const bool present = keyspace_.peek(key) != nullptr;
        const bool wants_present = options.condition == SetCondition::kIfPresent;
        if (present != wants_present) {
            return false;
        }
    }
    Entry& entry = keyspace_.put(key, std::move(value));
    if (options.ttl) {
        keyspace_.set_expiry(entry, keyspace_.now() + *options.ttl);
    }
    return true;
}

// ----- Sorted sets ----------------------------------------------------------------------------

std::size_t Database::zadd(std::string_view key, std::span<const ScoredMember> members) {
    Entry* entry = keyspace_.find(key);
    if (entry == nullptr) {
        if (members.empty()) {
            return 0;
        }
        entry = &keyspace_.put(key, SortedSet{});
    }
    SortedSet& set = value_as<SortedSet>(*entry);

    std::size_t added = 0;
    for (const auto& [member, score] : members) {
        if (set.add(member, score) == SortedSet::AddResult::kAdded) {
            ++added;
        }
    }
    return added;
}

std::size_t Database::zrem(std::string_view key, std::span<const std::string_view> members) {
    Entry* entry = keyspace_.find(key);
    if (entry == nullptr) {
        return 0;
    }
    SortedSet& set = value_as<SortedSet>(*entry);

    std::size_t removed = 0;
    for (const std::string_view member : members) {
        if (set.remove(member)) {
            ++removed;
        }
    }
    if (set.empty()) {
        keyspace_.erase(key);  // like Redis, never keep an empty aggregate around
    }
    return removed;
}

std::optional<double> Database::zscore(std::string_view key, std::string_view member) {
    const SortedSet* set = find_sorted_set_for_read(key);
    return set != nullptr ? set->score(member) : std::nullopt;
}

std::optional<std::size_t> Database::zrank(std::string_view key, std::string_view member) {
    const SortedSet* set = find_sorted_set_for_read(key);
    return set != nullptr ? set->rank(member) : std::nullopt;
}

std::size_t Database::zcard(std::string_view key) {
    const SortedSet* set = find_sorted_set_for_read(key);
    return set != nullptr ? set->size() : 0;
}

std::vector<ScoredMember> Database::zrange(std::string_view key, std::int64_t start,
                                           std::int64_t stop) {
    const SortedSet* set = find_sorted_set_for_read(key);
    return set != nullptr ? set->range_by_rank(start, stop) : std::vector<ScoredMember>{};
}

std::vector<ScoredMember> Database::zrange_by_score(std::string_view key, ScoreBound min,
                                                    ScoreBound max) {
    const SortedSet* set = find_sorted_set_for_read(key);
    return set != nullptr ? set->range_by_score(min, max) : std::vector<ScoredMember>{};
}

// ----- Helpers --------------------------------------------------------------------------------

Entry* Database::lookup_for_read(std::string_view key) {
    Entry* entry = keyspace_.find(key);
    if (entry != nullptr) {
        ++hits_;
    } else {
        ++misses_;
    }
    return entry;
}

const SortedSet* Database::find_sorted_set_for_read(std::string_view key) {
    Entry* entry = lookup_for_read(key);
    return entry != nullptr ? &value_as<SortedSet>(*entry) : nullptr;
}

}  // namespace memkv::storage
