#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "memkv/common/clock.hpp"
#include "memkv/ds/intrusive_list.hpp"
#include "memkv/storage/value.hpp"

namespace memkv::storage {

/// One key in the database: the key itself, its value and its optional expiration time.
///
/// Entry inherits ListHook so it can sit directly in the Keyspace's LRU list (an "intrusive"
/// list). Only the Keyspace may change the key or the expiration; everyone else gets read access
/// to them and read/write access to the value.
class Entry : public ds::ListHook {
public:
    Entry(std::string key, Value value) : key_(std::move(key)), value_(std::move(value)) {}

    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    [[nodiscard]] Value& value() noexcept { return value_; }
    [[nodiscard]] const Value& value() const noexcept { return value_; }
    [[nodiscard]] std::optional<TimePoint> expires_at() const noexcept { return expires_at_; }

private:
    friend class Keyspace;

    const std::string key_;  // never changes: the Keyspace index holds views into it
    Value value_;
    std::optional<TimePoint> expires_at_;
    std::uint64_t expiry_ticket_ = 0;  // identifies this entry's current ExpiryHeap item
};

}  // namespace memkv::storage
