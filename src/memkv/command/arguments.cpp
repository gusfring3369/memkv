#include "memkv/command/arguments.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>

#include "memkv/command/command_error.hpp"
#include "memkv/storage/database.hpp"

namespace memkv::command {
namespace {

constexpr std::string_view kNotAnInteger = "value is not an integer or out of range";
constexpr std::string_view kNotAFloat = "value is not a valid float";
constexpr std::string_view kBoundNotAFloat = "min or max is not a float";

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Parses the whole of `text` as a double, or throws CommandError(error_message).
double parse_double(std::string_view text, std::string_view error_message) {
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        throw CommandError(std::string(error_message));
    }
    const std::string terminated(text);  // strtod needs a NUL-terminated string
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(terminated.c_str(), &end);  // the server never changes locale
    const bool consumed_everything = end == terminated.c_str() + terminated.size();
    const bool overflowed = errno == ERANGE && std::isinf(value);
    if (!consumed_everything || overflowed || std::isnan(value)) {
        throw CommandError(std::string(error_message));
    }
    return value;
}

}  // namespace

bool Arguments::is(std::size_t index, std::string_view keyword) const {
    return equals_ignore_case(text(index), keyword);
}

std::int64_t Arguments::integer(std::size_t index) const {
    const std::string_view value = text(index);
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || error != std::errc() || end != value.data() + value.size()) {
        throw CommandError(std::string(kNotAnInteger));
    }
    return result;
}

double Arguments::score(std::size_t index) const {
    return parse_double(text(index), kNotAFloat);
}

storage::ScoreBound Arguments::score_bound(std::size_t index) const {
    const std::string_view value = text(index);
    if (value.starts_with('(')) {
        return {.value = parse_double(value.substr(1), kBoundNotAFloat), .exclusive = true};
    }
    return {.value = parse_double(value, kBoundNotAFloat), .exclusive = false};
}

Duration Arguments::ttl(std::size_t index, Duration unit, std::string_view command) const {
    const std::int64_t amount = integer(index);
    const std::int64_t limit = storage::Database::kMaxTtl / unit;
    if (amount > limit || amount < -limit) {
        throw CommandError("invalid expire time in '" + std::string(command) + "' command");
    }
    return unit * amount;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b,
                              [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

std::string to_lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), ascii_lower);
    return result;
}

}  // namespace memkv::command
