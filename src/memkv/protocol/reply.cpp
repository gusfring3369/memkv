#include "memkv/protocol/reply.hpp"

#include <algorithm>
#include <charconv>

#include "memkv/common/overloaded.hpp"

namespace memkv::protocol {
namespace {

constexpr std::string_view kCrlf = "\r\n";

/// Simple strings and errors are line-delimited, so they must not contain CR or LF.
std::string single_line(std::string text) {
    std::ranges::replace_if(text, [](char c) { return c == '\r' || c == '\n'; }, ' ');
    return text;
}

void append_integer(std::string& out, std::int64_t value) {
    char buffer[24];
    const auto [end, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
    out.append(buffer, end);
}

void append_length(std::string& out, char type, std::size_t length) {
    out += type;
    append_integer(out, static_cast<std::int64_t>(length));
    out += kCrlf;
}

}  // namespace

Reply Reply::simple(std::string text) {
    return Reply(SimpleString{single_line(std::move(text))});
}

Reply Reply::error(std::string message) {
    return Reply(Error{single_line(std::move(message))});
}

void encode(const Reply& reply, std::string& out) {
    std::visit(Overloaded{
                   [&out](const Reply::SimpleString& value) {
                       out += '+';
                       out += value.text;
                       out += kCrlf;
                   },
                   [&out](const Reply::Error& value) {
                       out += '-';
                       out += value.message;
                       out += kCrlf;
                   },
                   [&out](Reply::Integer value) {
                       out += ':';
                       append_integer(out, value);
                       out += kCrlf;
                   },
                   [&out](const Reply::BulkString& value) {
                       append_length(out, '$', value.size());
                       out += value;
                       out += kCrlf;
                   },
                   [&out](Reply::Null) { out += "$-1\r\n"; },
                   [&out](const Reply::Array& elements) {
                       append_length(out, '*', elements.size());
                       for (const Reply& element : elements) {
                           encode(element, out);
                       }
                   },
               },
               reply.storage());
}

}  // namespace memkv::protocol
