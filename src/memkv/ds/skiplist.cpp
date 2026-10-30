#include "memkv/ds/skiplist.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace memkv::ds {

/// A node and its tower of links live in a single allocation (the layout Redis uses):
/// [ Node | Level 0 | Level 1 | ... | Level height-1 ]. Separately allocating the links
/// (e.g. with std::vector) would cost an extra cache miss on every hop of every search.
struct SkipList::Node {
    struct Level {
        Node* forward = nullptr;
        std::size_t span = 0;  // level-0 nodes skipped by `forward` (distance to the end if null)
    };

    static Node* create(double score, std::string member, std::size_t height) {
        static_assert(alignof(Level) <= alignof(Node) && sizeof(Node) % alignof(Level) == 0,
                      "the level tower must be correctly aligned directly after the node");
        void* memory = ::operator new(sizeof(Node) + height * sizeof(Level));
        return ::new (memory) Node(score, std::move(member), height);
    }

    static void destroy(Node* node) noexcept {
        node->~Node();
        ::operator delete(node);
    }

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    [[nodiscard]] Level& level(std::size_t i) noexcept { return tower()[i]; }
    [[nodiscard]] const Level& level(std::size_t i) const noexcept { return tower()[i]; }

    /// (Re)creates every link of the tower in its empty state.
    void reset_levels() noexcept { std::uninitialized_value_construct_n(tower_storage(), height); }

    /// Strict (score, member) ordering: does this node sort before the given pair?
    [[nodiscard]] bool precedes(double s, std::string_view m) const {
        return score < s || (score == s && member < m);
    }

    [[nodiscard]] bool matches(double s, std::string_view m) const {
        return score == s && member == m;
    }

    std::string member;
    double score;
    std::size_t height;

private:
    Node(double s, std::string m, std::size_t h) : member(std::move(m)), score(s), height(h) {
        reset_levels();
    }
    ~Node() = default;

    [[nodiscard]] Level* tower_storage() noexcept {
        return reinterpret_cast<Level*>(reinterpret_cast<std::byte*>(this) + sizeof(Node));
    }
    [[nodiscard]] Level* tower() noexcept { return std::launder(tower_storage()); }
    [[nodiscard]] const Level* tower() const noexcept { return const_cast<Node*>(this)->tower(); }
};

namespace {

bool below_min(double score, ScoreBound min) {
    return min.exclusive ? score <= min.value : score < min.value;
}

bool above_max(double score, ScoreBound max) {
    return max.exclusive ? score >= max.value : score > max.value;
}

}  // namespace

SkipList::SkipList(std::uint64_t seed)
    : header_(Node::create(0.0, std::string(), kMaxLevel)), rng_state_(seed) {}

SkipList::~SkipList() {
    destroy();
}

SkipList::SkipList(SkipList&& other) noexcept
    : header_(std::exchange(other.header_, nullptr)),
      level_(std::exchange(other.level_, 1)),
      length_(std::exchange(other.length_, 0)),
      rng_state_(other.rng_state_) {}

SkipList& SkipList::operator=(SkipList&& other) noexcept {
    if (this != &other) {
        destroy();
        header_ = std::exchange(other.header_, nullptr);
        level_ = std::exchange(other.level_, 1);
        length_ = std::exchange(other.length_, 0);
        rng_state_ = other.rng_state_;
    }
    return *this;
}

void SkipList::destroy() noexcept {
    if (header_ == nullptr) {
        return;
    }
    // Iterative rather than recursive so that destroying a long list cannot overflow the stack.
    Node* node = header_->level(0).forward;
    while (node != nullptr) {
        Node* next = node->level(0).forward;
        Node::destroy(node);
        node = next;
    }
    Node::destroy(header_);
    header_ = nullptr;
}

std::size_t SkipList::random_level() {
    // splitmix64 step.
    std::uint64_t z = (rng_state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    z ^= z >> 31U;

    // Each pair of random bits promotes the node with probability 1/4; 64 bits cover 32 levels.
    std::size_t level = 1;
    while (level < kMaxLevel && (z & 3U) == 0) {
        ++level;
        z >>= 2U;
    }
    return level;
}

const std::string& SkipList::insert(double score, std::string member) {
    Node* node = Node::create(score, std::move(member), random_level());
    link(node);
    return node->member;
}

void SkipList::link(Node* node) {
    UpdatePath update{};
    std::array<std::size_t, kMaxLevel> rank{};  // rank of update[i]

    Node* x = header_;
    for (std::size_t i = level_; i-- > 0;) {
        rank[i] = (i + 1 == level_) ? 0 : rank[i + 1];
        while (x->level(i).forward != nullptr &&
               x->level(i).forward->precedes(node->score, node->member)) {
            rank[i] += x->level(i).span;
            x = x->level(i).forward;
        }
        update[i] = x;
    }

    const std::size_t height = node->height;
    if (height > level_) {
        for (std::size_t i = level_; i < height; ++i) {
            rank[i] = 0;
            update[i] = header_;
            header_->level(i).span = length_;
        }
        level_ = height;
    }

    for (std::size_t i = 0; i < height; ++i) {
        Node::Level& prev = update[i]->level(i);
        const std::size_t distance = rank[0] - rank[i];  // nodes between update[i] and update[0]
        node->level(i).forward = prev.forward;
        node->level(i).span = prev.span - distance;
        prev.forward = node;
        prev.span = distance + 1;
    }
    // Links above the new node's height now jump over one more node.
    for (std::size_t i = height; i < level_; ++i) {
        ++update[i]->level(i).span;
    }
    ++length_;
}

void SkipList::unlink(const Node* node, const UpdatePath& update) {
    for (std::size_t i = 0; i < level_; ++i) {
        Node::Level& prev = update[i]->level(i);
        if (prev.forward == node) {
            prev.span = prev.span + node->level(i).span - 1;
            prev.forward = node->level(i).forward;
        } else {
            --prev.span;
        }
    }
    while (level_ > 1 && header_->level(level_ - 1).forward == nullptr) {
        --level_;
    }
    --length_;
}

SkipList::Node* SkipList::find(double score, std::string_view member, UpdatePath& update) {
    Node* x = header_;
    for (std::size_t i = level_; i-- > 0;) {
        while (x->level(i).forward != nullptr && x->level(i).forward->precedes(score, member)) {
            x = x->level(i).forward;
        }
        update[i] = x;
    }
    Node* candidate = x->level(0).forward;
    return (candidate != nullptr && candidate->matches(score, member)) ? candidate : nullptr;
}

bool SkipList::erase(double score, std::string_view member) {
    UpdatePath update{};
    Node* node = find(score, member, update);
    if (node == nullptr) {
        return false;
    }
    unlink(node, update);
    Node::destroy(node);
    return true;
}

void SkipList::update_score(double old_score, std::string_view member, double new_score) {
    UpdatePath update{};
    Node* node = find(old_score, member, update);
    assert(node != nullptr && "update_score() requires an existing element");
    if (node == nullptr) {
        return;
    }
    unlink(node, update);
    node->score = new_score;
    node->reset_levels();
    link(node);
}

std::optional<std::size_t> SkipList::rank(double score, std::string_view member) const {
    std::size_t traversed = 0;
    const Node* x = header_;
    for (std::size_t i = level_; i-- > 0;) {
        while (x->level(i).forward != nullptr && (x->level(i).forward->precedes(score, member) ||
                                                  x->level(i).forward->matches(score, member))) {
            traversed += x->level(i).span;
            x = x->level(i).forward;
        }
        if (x != header_ && x->matches(score, member)) {
            return traversed - 1;
        }
    }
    return std::nullopt;
}

const SkipList::Node* SkipList::node_at_rank(std::size_t rank) const {
    // `rank` is 1-based here: the header sits at rank 0.
    std::size_t traversed = 0;
    const Node* x = header_;
    for (std::size_t i = level_; i-- > 0;) {
        while (x->level(i).forward != nullptr && traversed + x->level(i).span <= rank) {
            traversed += x->level(i).span;
            x = x->level(i).forward;
        }
        if (traversed == rank) {
            return x;
        }
    }
    return nullptr;
}

const SkipList::Node* SkipList::first_in_range(ScoreBound min) const {
    const Node* x = header_;
    for (std::size_t i = level_; i-- > 0;) {
        while (x->level(i).forward != nullptr && below_min(x->level(i).forward->score, min)) {
            x = x->level(i).forward;
        }
    }
    return x->level(0).forward;
}

std::vector<ScoredMember> SkipList::range_by_rank(std::size_t first, std::size_t last) const {
    std::vector<ScoredMember> result;
    if (first >= length_ || first > last) {
        return result;
    }
    const std::size_t count = std::min(last, length_ - 1) - first + 1;
    result.reserve(count);
    for (const Node* x = node_at_rank(first + 1); x != nullptr && result.size() < count;
         x = x->level(0).forward) {
        result.push_back({x->member, x->score});
    }
    return result;
}

std::vector<ScoredMember> SkipList::range_by_score(ScoreBound min, ScoreBound max) const {
    std::vector<ScoredMember> result;
    for (const Node* x = first_in_range(min); x != nullptr && !above_max(x->score, max);
         x = x->level(0).forward) {
        result.push_back({x->member, x->score});
    }
    return result;
}

}  // namespace memkv::ds
