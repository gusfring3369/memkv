#include "memkv/storage/sorted_set.hpp"

#include <string>

namespace memkv::storage {

SortedSet::AddResult SortedSet::add(std::string_view member, double score) {
    const auto it = scores_.find(member);
    if (it == scores_.end()) {
        const std::string& stored = ordered_.insert(score, std::string(member));
        scores_.emplace(stored, score);
        return AddResult::kAdded;
    }
    if (it->second == score) {
        return AddResult::kUnchanged;
    }
    ordered_.update_score(it->second, member, score);
    it->second = score;
    return AddResult::kUpdated;
}

bool SortedSet::remove(std::string_view member) {
    const auto it = scores_.find(member);
    if (it == scores_.end()) {
        return false;
    }
    const double score = it->second;
    // Erase the index entry first: its key is a view into the node the skip list is about to free.
    scores_.erase(it);
    ordered_.erase(score, member);
    return true;
}

std::optional<double> SortedSet::score(std::string_view member) const {
    const auto it = scores_.find(member);
    if (it == scores_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::size_t> SortedSet::rank(std::string_view member) const {
    const auto it = scores_.find(member);
    if (it == scores_.end()) {
        return std::nullopt;
    }
    return ordered_.rank(it->second, member);
}

std::vector<ScoredMember> SortedSet::range_by_rank(std::int64_t start, std::int64_t stop) const {
    const auto length = static_cast<std::int64_t>(size());
    if (start < 0) {
        start += length;
    }
    if (stop < 0) {
        stop += length;
    }
    if (start < 0) {
        start = 0;
    }
    if (start > stop || start >= length) {
        return {};
    }
    return ordered_.range_by_rank(static_cast<std::size_t>(start), static_cast<std::size_t>(stop));
}

std::vector<ScoredMember> SortedSet::range_by_score(ScoreBound min, ScoreBound max) const {
    return ordered_.range_by_score(min, max);
}

}  // namespace memkv::storage
