#include "memkv/ds/intrusive_list.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace memkv::ds {
namespace {

struct Item : ListHook {
    explicit Item(int v) : value(v) {}
    int value;
};

std::vector<int> contents(const IntrusiveList<Item>& list) {
    std::vector<int> values;
    list.for_each([&values](const Item& item) { values.push_back(item.value); });
    return values;
}

TEST(IntrusiveListTest, StartsEmpty) {
    IntrusiveList<Item> list;
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0U);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(IntrusiveListTest, PushFrontOrdersNewestFirst) {
    Item a(1), b(2), c(3);
    IntrusiveList<Item> list;
    list.push_front(a);
    list.push_front(b);
    list.push_front(c);
    EXPECT_EQ(contents(list), (std::vector<int>{3, 2, 1}));
    EXPECT_EQ(list.front(), &c);
    EXPECT_EQ(list.back(), &a);
    EXPECT_EQ(list.size(), 3U);
}

TEST(IntrusiveListTest, MoveToFrontFromAnyPosition) {
    Item a(1), b(2), c(3);
    IntrusiveList<Item> list;
    list.push_front(a);
    list.push_front(b);
    list.push_front(c);

    list.move_to_front(a);  // from the back
    EXPECT_EQ(contents(list), (std::vector<int>{1, 3, 2}));
    list.move_to_front(c);  // from the middle
    EXPECT_EQ(contents(list), (std::vector<int>{3, 1, 2}));
    list.move_to_front(c);  // already at the front
    EXPECT_EQ(contents(list), (std::vector<int>{3, 1, 2}));
    EXPECT_EQ(list.size(), 3U);
}

TEST(IntrusiveListTest, EraseUnlinksElement) {
    Item a(1), b(2), c(3);
    IntrusiveList<Item> list;
    list.push_front(a);
    list.push_front(b);
    list.push_front(c);

    list.erase(b);
    EXPECT_FALSE(b.is_linked());
    EXPECT_EQ(contents(list), (std::vector<int>{3, 1}));
    list.erase(a);
    list.erase(c);
    EXPECT_TRUE(list.empty());
}

}  // namespace
}  // namespace memkv::ds
