#include "memkv/protocol/reply.hpp"

#include <gtest/gtest.h>

#include <string>

namespace memkv::protocol {
namespace {

std::string encoded(const Reply& reply) {
    std::string out;
    encode(reply, out);
    return out;
}

TEST(ReplyTest, EncodesScalarTypes) {
    EXPECT_EQ(encoded(Reply::ok()), "+OK\r\n");
    EXPECT_EQ(encoded(Reply::error("ERR boom")), "-ERR boom\r\n");
    EXPECT_EQ(encoded(Reply::integer(-42)), ":-42\r\n");
    EXPECT_EQ(encoded(Reply::bulk("hello")), "$5\r\nhello\r\n");
    EXPECT_EQ(encoded(Reply::bulk("")), "$0\r\n\r\n");
    EXPECT_EQ(encoded(Reply::null()), "$-1\r\n");
}

TEST(ReplyTest, EncodesNestedArrays) {
    const Reply reply = Reply::array({Reply::bulk("a"), Reply::integer(1), Reply::array({})});
    EXPECT_EQ(encoded(reply), "*3\r\n$1\r\na\r\n:1\r\n*0\r\n");
}

TEST(ReplyTest, LineRepliesCannotBreakFraming) {
    EXPECT_EQ(encoded(Reply::error("ERR bad\r\nname")), "-ERR bad  name\r\n");
    EXPECT_EQ(encoded(Reply::simple("a\nb")), "+a b\r\n");
}

}  // namespace
}  // namespace memkv::protocol
