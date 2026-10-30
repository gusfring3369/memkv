#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "memkv/common/clock.hpp"

namespace memkv::ds {

/// Min-heap of key deadlines, used for active expiration.
///
/// Items are never removed eagerly when a key's TTL changes or the key is deleted: deleting from
/// the middle of a binary heap would need an extra position index. Instead every item carries
/// the `ticket` that was current for its key when it was pushed, and the owner treats items whose
/// ticket no longer matches as stale (lazy invalidation). remove_if() lets the owner purge stale
/// items in bulk so they cannot accumulate without bound.
class ExpiryHeap {
public:
    struct Item {
        TimePoint deadline;
        std::uint64_t ticket = 0;
        std::string key;
    };

    void push(Item item) {
        items_.push_back(std::move(item));
        std::push_heap(items_.begin(), items_.end(), later_deadline);
    }

    /// Removes and returns the earliest item if its deadline is at or before `now`.
    std::optional<Item> pop_due(TimePoint now) {
        if (items_.empty() || items_.front().deadline > now) {
            return std::nullopt;
        }
        std::pop_heap(items_.begin(), items_.end(), later_deadline);
        Item item = std::move(items_.back());
        items_.pop_back();
        return item;
    }

    /// Drops every item matching `predicate` and restores the heap property, in O(n).
    template <typename Predicate>
    void remove_if(Predicate predicate) {
        std::erase_if(items_, predicate);
        std::make_heap(items_.begin(), items_.end(), later_deadline);
    }

    void clear() noexcept { items_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

private:
    // std heap algorithms build max-heaps; inverting the comparison yields a min-heap.
    static bool later_deadline(const Item& a, const Item& b) { return a.deadline > b.deadline; }

    std::vector<Item> items_;
};

}  // namespace memkv::ds
