#pragma once

// Internal to SkipList: only skiplist.cpp includes this file.

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "memkv/ds/skiplist.hpp"

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

}  // namespace memkv::ds
