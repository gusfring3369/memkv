#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace memkv::ds {

struct ScoredMember {
    std::string member;
    double score = 0.0;

    friend bool operator==(const ScoredMember&, const ScoredMember&) = default;
};

/// One end of a score interval. `exclusive` turns `<=` into `<` (Redis' "(1.5" syntax).
struct ScoreBound {
    double value = 0.0;
    bool exclusive = false;
};

/// Probabilistic skip list of (score, member) pairs, ordered by score and then by member.
///
/// Every forward link also stores its *span*: the number of level-0 nodes it jumps over. This is
/// the augmentation Redis uses in its zskiplist. Summing spans along a search path yields an
/// element's rank, so rank lookups and rank-based range queries cost O(log N) rather than O(N).
///
/// Promotion probability is 1/4 with at most 32 levels (Redis' parameters): on average 1.33
/// links per node, and enough levels for 2^64 elements.
///
/// The list does not de-duplicate members; SortedSet owns that invariant.
class SkipList {
public:
    static constexpr std::size_t kMaxLevel = 32;

    explicit SkipList(std::uint64_t seed = std::random_device{}());
    ~SkipList();

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    /// A moved-from SkipList may only be destroyed or assigned to.
    SkipList(SkipList&& other) noexcept;
    SkipList& operator=(SkipList&& other) noexcept;

    /// Inserts a pair that is not already present, in O(log N). The returned reference to the
    /// stored member stays valid until the pair is erased, including across update_score().
    const std::string& insert(double score, std::string member);

    /// Removes the pair in O(log N). Returns false if it was not present.
    bool erase(double score, std::string_view member);

    /// Moves an existing pair to a new score in O(log N). The node is re-linked rather than
    /// reallocated, so references returned by insert() remain valid.
    void update_score(double old_score, std::string_view member, double new_score);

    /// 0-based position of the pair in sorted order, in O(log N).
    [[nodiscard]] std::optional<std::size_t> rank(double score, std::string_view member) const;

    /// Elements whose 0-based rank lies in [first, last], in O(log N + K). `last` is clamped.
    [[nodiscard]] std::vector<ScoredMember> range_by_rank(std::size_t first,
                                                          std::size_t last) const;

    /// Elements whose score lies between the bounds, in O(log N + K).
    [[nodiscard]] std::vector<ScoredMember> range_by_score(ScoreBound min, ScoreBound max) const;

    [[nodiscard]] std::size_t size() const noexcept { return length_; }
    [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

private:
    struct Node;
    /// For each level, the last node before the position being searched for.
    using UpdatePath = std::array<Node*, kMaxLevel>;

    [[nodiscard]] std::size_t random_level();
    void link(Node* node);
    void unlink(const Node* node, const UpdatePath& update);
    Node* find(double score, std::string_view member, UpdatePath& update);
    [[nodiscard]] const Node* node_at_rank(std::size_t rank) const;
    [[nodiscard]] const Node* first_in_range(ScoreBound min) const;
    void destroy() noexcept;

    Node* header_;
    std::size_t level_ = 1;
    std::size_t length_ = 0;
    std::uint64_t rng_state_;  // splitmix64: 8 bytes of state instead of mt19937's 2.5 KB
};

}  // namespace memkv::ds
