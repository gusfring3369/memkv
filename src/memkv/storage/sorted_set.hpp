#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "memkv/ds/skiplist.hpp"

namespace memkv::storage {

using ds::ScoreBound;
using ds::ScoredMember;

/// A Redis-style sorted set: unique members, each with a score, kept in (score, member) order.
///
/// Two structures index the same elements, each answering the queries the other cannot:
///   * a hash map  member -> score   for O(1) ZSCORE and duplicate detection;
///   * a skip list (score, member)   for O(log N) ZRANK and O(log N + K) range queries.
/// The hash map's keys are views into the member strings owned by the skip list nodes, so each
/// member is stored exactly once.
class SortedSet {
public:
    enum class AddResult { kAdded, kUpdated, kUnchanged };

    SortedSet() = default;

    AddResult add(std::string_view member, double score);
    bool remove(std::string_view member);

    [[nodiscard]] std::optional<double> score(std::string_view member) const;
    [[nodiscard]] std::optional<std::size_t> rank(std::string_view member) const;

    /// Redis ZRANGE semantics: inclusive 0-based indices, negative values count from the end.
    [[nodiscard]] std::vector<ScoredMember> range_by_rank(std::int64_t start,
                                                          std::int64_t stop) const;
    [[nodiscard]] std::vector<ScoredMember> range_by_score(ScoreBound min, ScoreBound max) const;

    [[nodiscard]] std::size_t size() const noexcept { return scores_.size(); }
    [[nodiscard]] bool empty() const noexcept { return scores_.empty(); }

private:
    ds::SkipList ordered_;
    std::unordered_map<std::string_view, double> scores_;
};

}  // namespace memkv::storage
