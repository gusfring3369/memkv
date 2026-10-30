#pragma once

#include <cstdint>

namespace memkv::ds {

/// SplitMix64: a tiny, fast pseudo-random number generator with good statistical quality.
///
/// Used to pick skip-list node heights. Its whole state is one 64-bit integer, which matters
/// because every sorted set owns a generator.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

private:
    std::uint64_t state_;
};

}  // namespace memkv::ds
