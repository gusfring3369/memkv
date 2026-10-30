#pragma once

#include <concepts>
#include <cstddef>

namespace memkv::ds {

/// Link fields embedded in every object that can be placed in an IntrusiveList.
///
/// Storing the links inside the element (instead of allocating separate nodes, as std::list
/// does) means that whoever holds a reference to an element can unlink or move it in O(1)
/// without a search and without any allocation.
class ListHook {
public:
    ListHook() = default;
    ListHook(const ListHook&) = delete;
    ListHook& operator=(const ListHook&) = delete;
    ~ListHook() = default;

    [[nodiscard]] bool is_linked() const noexcept { return next_ != nullptr; }

private:
    template <typename T>
    friend class IntrusiveList;

    ListHook* prev_ = nullptr;
    ListHook* next_ = nullptr;
};

/// Non-owning, circular doubly linked list with a sentinel node.
///
/// Because of the sentinel, every real element always has a valid predecessor and successor,
/// so linking and unlinking never branch on "empty list", "first element" or "last element".
/// All operations are O(1).
template <typename T>
class IntrusiveList {
    static_assert(std::derived_from<T, ListHook>, "elements must derive from ListHook");

public:
    IntrusiveList() noexcept { reset(); }
    // Elements point back at the sentinel, so the list cannot be copied or relocated.
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;
    IntrusiveList(IntrusiveList&&) = delete;
    IntrusiveList& operator=(IntrusiveList&&) = delete;
    ~IntrusiveList() = default;

    [[nodiscard]] bool empty() const noexcept { return sentinel_.next_ == &sentinel_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] T* front() noexcept { return empty() ? nullptr : downcast(sentinel_.next_); }
    [[nodiscard]] T* back() noexcept { return empty() ? nullptr : downcast(sentinel_.prev_); }

    void push_front(T& item) noexcept {
        link_after(sentinel_, item);
        ++size_;
    }

    void erase(T& item) noexcept {
        unlink(item);
        --size_;
    }

    void move_to_front(T& item) noexcept {
        unlink(item);
        link_after(sentinel_, item);
    }

    /// Forgets every element without touching them; used when the elements are destroyed in bulk.
    void reset() noexcept {
        sentinel_.prev_ = &sentinel_;
        sentinel_.next_ = &sentinel_;
        size_ = 0;
    }

    /// Visits elements from front to back.
    template <typename Visitor>
    void for_each(Visitor&& visit) const {
        for (const ListHook* hook = sentinel_.next_; hook != &sentinel_; hook = hook->next_) {
            visit(static_cast<const T&>(*hook));
        }
    }

private:
    static void link_after(ListHook& position, ListHook& node) noexcept {
        node.prev_ = &position;
        node.next_ = position.next_;
        position.next_->prev_ = &node;
        position.next_ = &node;
    }

    static void unlink(ListHook& node) noexcept {
        node.prev_->next_ = node.next_;
        node.next_->prev_ = node.prev_;
        node.prev_ = nullptr;
        node.next_ = nullptr;
    }

    static T* downcast(ListHook* hook) noexcept { return static_cast<T*>(hook); }

    ListHook sentinel_;
    std::size_t size_ = 0;
};

}  // namespace memkv::ds
