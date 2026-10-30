#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "memkv/common/clock.hpp"

/// Small value types used in the Database API.
namespace memkv::storage {

/// When SET is allowed to write.
enum class SetCondition {
    kAlways,
    kIfAbsent,   // NX: only create
    kIfPresent,  // XX: only overwrite
};

struct SetOptions {
    std::optional<Duration> ttl;  // empty: the key never expires
    SetCondition condition = SetCondition::kAlways;
};

/// Answer to "how long until this key expires?".
struct TimeToLive {
    enum class State { kMissing, kPersistent, kExpiring };

    State state = State::kMissing;
    Duration remaining{0};  // meaningful only when state == kExpiring
};

struct DatabaseStats {
    std::size_t keys = 0;
    std::size_t keys_with_expiry = 0;
    std::uint64_t hits = 0;    // reads that found their key
    std::uint64_t misses = 0;  // reads that did not
    std::uint64_t expired_keys = 0;
    std::uint64_t evicted_keys = 0;
};

}  // namespace memkv::storage
