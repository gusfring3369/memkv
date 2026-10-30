#pragma once

#include <stdexcept>

namespace memkv::storage {

/// Thrown when an operation for one data type targets a key holding another, e.g. GET on a
/// sorted set.
class WrongTypeError : public std::runtime_error {
public:
    WrongTypeError()
        : std::runtime_error("operation against a key holding the wrong kind of value") {}
};

}  // namespace memkv::storage
