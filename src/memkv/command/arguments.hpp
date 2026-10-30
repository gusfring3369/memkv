#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "memkv/common/clock.hpp"
#include "memkv/storage/sorted_set.hpp"

namespace memkv::command {

/// The arguments that follow a command's name, with accessors that convert and validate.
///
/// Index 0 is the first argument *after* the name: for `SET user:1 alice`, text(0) is
/// "user:1" and text(1) is "alice". Every typed accessor throws CommandError with a
/// Redis-compatible message when the argument is malformed, so command code reads as
/// straight-line logic without error checks.
class Arguments {
public:
    explicit Arguments(std::span<const std::string_view> values) : values_(values) {}

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

    /// The raw argument. Valid only while the request is being executed.
    [[nodiscard]] std::string_view text(std::size_t index) const { return values_[index]; }

    /// An owned copy, for values that are stored beyond the request.
    [[nodiscard]] std::string string(std::size_t index) const { return std::string(text(index)); }

    /// Arguments from `first` to the end, e.g. all keys of `DEL k1 k2 k3`.
    [[nodiscard]] std::span<const std::string_view> from(std::size_t first) const {
        return values_.subspan(first);
    }

    /// Case-insensitive comparison with an option keyword such as "WITHSCORES".
    [[nodiscard]] bool is(std::size_t index, std::string_view keyword) const;

    [[nodiscard]] std::int64_t integer(std::size_t index) const;

    /// A finite or infinite number ("1.5", "-3e2", "+inf"); NaN is rejected.
    [[nodiscard]] double score(std::size_t index) const;

    /// A ZRANGEBYSCORE bound: "1.5" is inclusive, "(1.5" is exclusive.
    [[nodiscard]] storage::ScoreBound score_bound(std::size_t index) const;

    /// A TTL given as an integer count of `unit`, e.g. seconds for EXPIRE.
    /// `command` names the command in the error message.
    [[nodiscard]] Duration ttl(std::size_t index, Duration unit, std::string_view command) const;

private:
    std::span<const std::string_view> values_;
};

/// ASCII case-insensitive equality (commands and options are always ASCII).
[[nodiscard]] bool equals_ignore_case(std::string_view a, std::string_view b);

/// ASCII lower-case copy of `text`.
[[nodiscard]] std::string to_lower(std::string_view text);

}  // namespace memkv::command
