#pragma once

namespace memkv {

/// Builds a visitor from a set of lambdas, for use with std::visit.
template <typename... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

}  // namespace memkv
