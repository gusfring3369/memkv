#include "memkv/protocol/request_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>

namespace memkv::protocol {
namespace {

struct Line {
    std::string_view text;  // without the terminator
    std::size_t next;       // offset just past the terminator
};

/// Finds the line starting at `pos`. Lines end in "\r\n"; a bare "\n" is tolerated.
std::optional<Line> find_line(std::string_view input, std::size_t pos) {
    const std::size_t newline = input.find('\n', pos);
    if (newline == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view text = input.substr(pos, newline - pos);
    if (!text.empty() && text.back() == '\r') {
        text.remove_suffix(1);
    }
    return Line{text, newline + 1};
}

std::optional<std::int64_t> parse_int(std::string_view text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

void split_whitespace(std::string_view line, std::vector<std::string_view>& tokens) {
    tokens.clear();
    std::size_t pos = 0;
    while (pos < line.size()) {
        const std::size_t start = line.find_first_not_of(" \t", pos);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(line.find_first_of(" \t", start), line.size());
        tokens.push_back(line.substr(start, end - start));
        pos = end;
    }
}

}  // namespace

RequestParser::Result RequestParser::parse(std::string_view input,
                                           std::vector<std::string_view>& args) {
    // Step 1, between requests: find where the next request starts. An inline command is
    // complete as soon as its line is; a multibulk header ("*3") starts a request whose
    // arguments follow.
    std::size_t start = 0;  // blank lines and empty arrays before the request are skipped
    while (!inside_multibulk()) {
        const std::optional<Line> line = find_line(input, start);
        if (!line) {
            if (input.size() - start > limits_.max_inline_length) {
                return fail("request line too long");
            }
            return {Status::kNeedMoreData, start};
        }
        if (line->text.starts_with('*')) {
            const std::optional<std::int64_t> count = parse_int(line->text.substr(1));
            if (!count || *count > static_cast<std::int64_t>(limits_.max_array_length)) {
                return fail("invalid multibulk length");
            }
            if (*count > 0) {
                expected_args_ = static_cast<std::size_t>(*count);
                resume_offset_ = line->next - start;
                break;
            }
        } else {
            split_whitespace(line->text, args);
            if (!args.empty()) {
                return {Status::kCommandReady, line->next};
            }
        }
        start = line->next;  // a blank line, "*0" or "*-1": nothing to run
    }

    // Step 2: read the multibulk request's arguments.
    return parse_bulk_strings(input.substr(start), start, args);
}

RequestParser::Result RequestParser::parse_bulk_strings(std::string_view request,
                                                        std::size_t request_offset,
                                                        std::vector<std::string_view>& args) {
    // Each argument is "$<length>\r\n<bytes>\r\n". Continue where the previous call stopped.
    std::size_t pos = resume_offset_;
    const auto wait_for_more_data = [&] {
        resume_offset_ = pos;
        return Result{Status::kNeedMoreData, request_offset};  // keep the request's bytes
    };

    while (spans_.size() < expected_args_) {
        if (!bulk_length_) {
            const std::optional<Line> header = find_line(request, pos);
            if (!header) {
                if (request.size() - pos > limits_.max_inline_length) {
                    return fail("bulk header too long");
                }
                return wait_for_more_data();
            }
            if (!header->text.starts_with('$')) {
                return fail("expected '$' at start of bulk string");
            }
            const std::optional<std::int64_t> length = parse_int(header->text.substr(1));
            if (!length || *length < 0 ||
                *length > static_cast<std::int64_t>(limits_.max_bulk_length)) {
                return fail("invalid bulk length");
            }
            bulk_length_ = static_cast<std::size_t>(*length);
            pos = header->next;
        }

        const std::size_t length = *bulk_length_;
        if (request.size() - pos < length + 2) {
            return wait_for_more_data();
        }
        if (request.substr(pos + length, 2) != "\r\n") {
            return fail("bulk string not terminated by CRLF");
        }
        spans_.push_back({.offset = pos, .length = length});
        pos += length + 2;
        bulk_length_.reset();
    }

    // Complete: hand out views into the request, then get ready for the next one.
    args.clear();
    for (const Span& span : spans_) {
        args.push_back(request.substr(span.offset, span.length));
    }
    reset();
    return {Status::kCommandReady, request_offset + pos};
}

void RequestParser::reset() noexcept {
    expected_args_ = 0;
    resume_offset_ = 0;
    bulk_length_.reset();
    spans_.clear();
}

RequestParser::Result RequestParser::fail(std::string message) {
    error_ = std::move(message);
    reset();
    return {Status::kProtocolError, 0};
}

}  // namespace memkv::protocol
