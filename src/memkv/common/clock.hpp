#pragma once

#include <chrono>

namespace memkv {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

/// Monotonic time source.
///
/// Expiration logic depends on this interface instead of calling std::chrono directly so that
/// tests can move time forward deterministically instead of sleeping.
class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual TimePoint now() const = 0;
};

/// Production clock. steady_clock is used rather than system_clock so that TTLs are immune to
/// wall-clock adjustments (NTP corrections, manual time changes).
class SteadyClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const override { return std::chrono::steady_clock::now(); }
};

}  // namespace memkv
