#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace memkv::protocol {

/// A reply in RESP2, the Redis wire protocol. Constructed through the named factories.
class Reply {
public:
    struct SimpleString {
        std::string text;
    };
    struct Error {
        std::string message;
    };
    struct Null {};
    using Integer = std::int64_t;
    using BulkString = std::string;
    using Array = std::vector<Reply>;
    using Storage = std::variant<SimpleString, Error, Integer, BulkString, Null, Array>;

    static Reply ok() { return Reply(SimpleString{"OK"}); }
    static Reply simple(std::string text);
    /// `message` should start with an error code such as "ERR" or "WRONGTYPE".
    static Reply error(std::string message);
    static Reply integer(Integer value) { return Reply(value); }
    static Reply bulk(std::string data) { return Reply(BulkString(std::move(data))); }
    static Reply null() { return Reply(Null{}); }
    static Reply array(Array elements) { return Reply(std::move(elements)); }

    [[nodiscard]] const Storage& storage() const noexcept { return storage_; }

private:
    explicit Reply(Storage storage) : storage_(std::move(storage)) {}

    Storage storage_;
};

/// Appends the RESP2 encoding of `reply` to `out`.
void encode(const Reply& reply, std::string& out);

}  // namespace memkv::protocol
