#pragma once

#include <string>
#include <variant>

#include "memkv/storage/sorted_set.hpp"

namespace memkv::storage {

/// The value stored under a key: either a string or a sorted set.
///
/// std::variant holds exactly one of its alternatives at a time and remembers which one, so a
/// key can change type (SET over a sorted set) without inheritance or manual memory management.
/// Supporting a new data type means adding an alternative here and to ValueType.
using Value = std::variant<std::string, SortedSet>;

enum class ValueType { kNone, kString, kSortedSet };

[[nodiscard]] inline ValueType type_of(const Value& value) {
    return std::holds_alternative<std::string>(value) ? ValueType::kString : ValueType::kSortedSet;
}

}  // namespace memkv::storage
