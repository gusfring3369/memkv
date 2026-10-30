#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace memkv::protocol {

/// Caps that stop a misbehaving client from making the server buffer unbounded input.
/// Defaults match Redis.
struct ParserLimits {
    std::size_t max_inline_length = 64 * 1024;
    std::size_t max_bulk_length = 512 * 1024 * 1024;
    std::size_t max_array_length = 1024 * 1024;
};

/// Incremental, zero-copy parser for client requests in either RESP request form:
///   * multibulk: "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n"  (sent by redis-cli and client libraries)
///   * inline:    "GET key\r\n"                        (convenient from telnet or nc)
///
/// Arguments are returned as views into the caller's buffer rather than copied into new
/// strings, so parsing a request performs no heap allocation.
///
/// Input may arrive split at arbitrary byte boundaries. Bytes of an incomplete request are not
/// consumed (the views must point at them once it completes), but the parser remembers how far
/// it got, so completed elements are never re-scanned and waiting for a large bulk string costs
/// O(1) per call.
class RequestParser {
public:
    enum class Status { kNeedMoreData, kCommandReady, kProtocolError };

    struct Result {
        Status status;
        /// Bytes at the front of `input` that the caller must discard before the next call.
        std::size_t consumed;
    };

    explicit RequestParser(ParserLimits limits = {}) : limits_(limits) {}

    /// Parses from the front of `input`, which must start with the bytes that were not consumed
    /// by the previous call. On kCommandReady, `args` holds the complete argument vector (never
    /// empty) as views into `input`, valid until the caller modifies its buffer. On
    /// kProtocolError, error() describes the problem and the connection should be closed.
    Result parse(std::string_view input, std::vector<std::string_view>& args);

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    struct Span {
        std::size_t offset;  // relative to the start of the current request
        std::size_t length;
    };

    void reset() noexcept;
    Result fail(std::string message);

    ParserLimits limits_;
    // Progress through the current, incomplete multibulk request.
    std::size_t expected_args_ = 0;           // 0 while between requests
    std::size_t resume_offset_ = 0;           // where to continue scanning
    std::optional<std::size_t> bulk_length_;  // header read, payload still awaited
    std::vector<Span> spans_;
    std::string error_;
};

}  // namespace memkv::protocol
