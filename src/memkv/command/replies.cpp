#include "memkv/command/replies.hpp"

#include <array>
#include <charconv>
#include <cstdint>

namespace memkv::command::replies {

using protocol::Reply;

Reply count(std::size_t value) {
    return Reply::integer(static_cast<std::int64_t>(value));
}

Reply boolean(bool value) {
    return Reply::integer(value ? 1 : 0);
}

std::string format_score(double score) {
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), score);
    return {buffer.data(), end};
}

Reply scored_members(std::vector<storage::ScoredMember> members, bool with_scores) {
    Reply::Array elements;
    elements.reserve(members.size() * (with_scores ? 2 : 1));
    for (auto& [member, score] : members) {
        elements.push_back(Reply::bulk(std::move(member)));
        if (with_scores) {
            elements.push_back(Reply::bulk(format_score(score)));
        }
    }
    return Reply::array(std::move(elements));
}

Reply time_to_live(const storage::TimeToLive& ttl, bool in_seconds) {
    switch (ttl.state) {
        case storage::TimeToLive::State::kMissing:
            return Reply::integer(-2);
        case storage::TimeToLive::State::kPersistent:
            return Reply::integer(-1);
        case storage::TimeToLive::State::kExpiring:
            break;
    }
    const std::int64_t milliseconds = ttl.remaining.count();
    // Like Redis, TTL rounds to the nearest second.
    return Reply::integer(in_seconds ? (milliseconds + 500) / 1000 : milliseconds);
}

}  // namespace memkv::command::replies
