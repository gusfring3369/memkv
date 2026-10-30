#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "memkv/protocol/reply.hpp"
#include "memkv/storage/database.hpp"

/// Small builders for replies that several commands share.
namespace memkv::command::replies {

/// A count, e.g. how many keys DEL removed.
[[nodiscard]] protocol::Reply count(std::size_t value);

/// Redis represents booleans as the integers 1 and 0.
[[nodiscard]] protocol::Reply boolean(bool value);

/// Shortest text that reads back as the same double, e.g. "1250.5" or "inf".
[[nodiscard]] std::string format_score(double score);

/// A ZRANGE-style array: members only, or member/score pairs when `with_scores` is set.
[[nodiscard]] protocol::Reply scored_members(std::vector<storage::ScoredMember> members,
                                             bool with_scores);

/// TTL/PTTL semantics: -2 for a missing key, -1 for no TTL, otherwise the time left.
[[nodiscard]] protocol::Reply time_to_live(const storage::TimeToLive& ttl, bool in_seconds);

}  // namespace memkv::command::replies
