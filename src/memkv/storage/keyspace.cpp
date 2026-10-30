#include "memkv/storage/keyspace.hpp"

namespace memkv::storage {
namespace {

// Below this size stale heap items are cheap enough to leave for expire_due() to discard.
constexpr std::size_t kMinHeapSizeToCompact = 1024;

}  // namespace

Keyspace::Keyspace(const Clock& clock, KeyspaceConfig config) : clock_(clock), config_(config) {}

Entry* Keyspace::find(std::string_view key) {
    Entry* entry = lookup(key);
    if (entry != nullptr) {
        lru_.move_to_front(*entry);
    }
    return entry;
}

Entry* Keyspace::peek(std::string_view key) {
    return lookup(key);
}

Entry* Keyspace::lookup(std::string_view key) {
    const auto it = index_.find(key);
    if (it == index_.end()) {
        return nullptr;
    }
    if (has_expired(*it->second)) {  // lazy expiration
        remove(it);
        ++stats_.expired_keys;
        return nullptr;
    }
    return it->second.get();
}

Entry& Keyspace::put(std::string_view key, Value value) {
    if (Entry* existing = find(key)) {
        existing->value_ = std::move(value);
        clear_expiry(*existing);
        return *existing;
    }
    // Build the entry before evicting, in case `key` views memory owned by the evicted entry.
    auto entry = std::make_unique<Entry>(std::string(key), std::move(value));
    evict_until_below_capacity();

    Entry& stored = *entry;
    index_.emplace(stored.key(), std::move(entry));
    lru_.push_front(stored);
    return stored;
}

bool Keyspace::erase(std::string_view key) {
    const auto it = index_.find(key);
    if (it == index_.end()) {
        return false;
    }
    const bool expired = has_expired(*it->second);
    remove(it);
    if (expired) {
        ++stats_.expired_keys;
    }
    return !expired;
}

void Keyspace::set_expiry(Entry& entry, TimePoint deadline) {
    if (!entry.expires_at_) {
        ++keys_with_expiry_;
    }
    entry.expires_at_ = deadline;
    entry.expiry_ticket_ = next_expiry_ticket_++;
    expiry_heap_.push({deadline, entry.expiry_ticket_, entry.key_});
    compact_expiry_heap_if_needed();
}

void Keyspace::clear_expiry(Entry& entry) {
    if (!entry.expires_at_) {
        return;
    }
    // The entry's heap item becomes stale; it is discarded when popped or compacted.
    entry.expires_at_.reset();
    entry.expiry_ticket_ = 0;
    --keys_with_expiry_;
}

std::size_t Keyspace::expire_due(std::size_t max_checks) {
    const TimePoint now = clock_.now();
    std::size_t removed = 0;
    for (std::size_t checks = 0; checks < max_checks; ++checks) {
        std::optional<ds::ExpiryHeap::Item> item = expiry_heap_.pop_due(now);
        if (!item) {
            break;
        }
        const auto it = index_.find(item->key);
        if (it == index_.end() || it->second->expiry_ticket_ != item->ticket) {
            continue;  // stale: the key was deleted or its TTL changed after this push
        }
        remove(it);
        ++stats_.expired_keys;
        ++removed;
    }
    return removed;
}

void Keyspace::clear() noexcept {
    lru_.reset();
    index_.clear();
    expiry_heap_.clear();
    keys_with_expiry_ = 0;
}

std::vector<std::string> Keyspace::keys_by_recency() const {
    std::vector<std::string> keys;
    keys.reserve(lru_.size());
    lru_.for_each([&keys](const Entry& entry) { keys.push_back(entry.key()); });
    return keys;
}

void Keyspace::remove(Index::iterator it) {
    Entry& entry = *it->second;
    if (entry.expires_at_) {
        --keys_with_expiry_;
    }
    lru_.erase(entry);
    index_.erase(it);
}

void Keyspace::evict_until_below_capacity() {
    if (config_.max_keys == 0) {
        return;
    }
    while (index_.size() >= config_.max_keys) {
        const Entry* victim = lru_.back();
        if (victim == nullptr) {
            return;  // unreachable while index and list agree; never dereference null
        }
        remove(index_.find(victim->key()));
        ++stats_.evicted_keys;
    }
}

void Keyspace::compact_expiry_heap_if_needed() {
    // Each entry with a TTL has exactly one current heap item, so the heap holds
    // keys_with_expiry_ current items plus some stale ones. Once stale items are the majority,
    // purge them in one O(n) pass. At least n/2 pushes happen between two passes, so the
    // amortized cost per push stays O(1) on top of the O(log n) push itself, and memory stays
    // proportional to the number of keys that actually have a TTL.
    const std::size_t heap_size = expiry_heap_.size();
    if (heap_size < kMinHeapSizeToCompact || heap_size <= 2 * keys_with_expiry_) {
        return;
    }
    expiry_heap_.remove_if([this](const ds::ExpiryHeap::Item& item) { return !is_current(item); });
}

bool Keyspace::has_expired(const Entry& entry) const {
    return entry.expires_at_ && *entry.expires_at_ <= clock_.now();
}

bool Keyspace::is_current(const ds::ExpiryHeap::Item& item) const {
    const auto it = index_.find(item.key);
    return it != index_.end() && it->second->expiry_ticket_ == item.ticket;
}

}  // namespace memkv::storage
