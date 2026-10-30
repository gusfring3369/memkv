#pragma once

#include "memkv/common/clock.hpp"

namespace memkv::testing {

/// Clock that only moves when told to, so TTL tests are fast and deterministic.
class ManualClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const override { return now_; }
    void advance(Duration duration) { now_ += duration; }

private:
    TimePoint now_{std::chrono::hours(1)};
};

}  // namespace memkv::testing
