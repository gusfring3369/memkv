#include "memkv/protocol/request_parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace memkv::protocol {
namespace {

using Args = std::vector<std::string>;
using Views = std::vector<std::string_view>;
using Status = RequestParser::Status;

/// Feeds `input` to the parser in one go and returns every command it produced. Arguments are
/// copied out before the buffer changes, since the parser hands out views into it.
std::vector<Args> parse_all(RequestParser& parser, std::string input) {
    std::vector<Args> commands;
    Views views;
    while (true) {
        const auto [status, consumed] = parser.parse(input, views);
        if (status != Status::kCommandReady) {
            EXPECT_EQ(status, Status::kNeedMoreData) << parser.error();
            return commands;
        }
        commands.emplace_back(views.begin(), views.end());
        input.erase(0, consumed);
    }
}

TEST(RequestParserTest, ParsesMultibulkCommand) {
    RequestParser parser;
    EXPECT_EQ(parse_all(parser, "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhello\r\n"),
              (std::vector<Args>{{"SET", "k", "hello"}}));
}

TEST(RequestParserTest, ParsesInlineCommand) {
    RequestParser parser;
    EXPECT_EQ(parse_all(parser, "SET  k\thello\r\nPING\n"),
              (std::vector<Args>{{"SET", "k", "hello"}, {"PING"}}));
}

TEST(RequestParserTest, SkipsBlankLinesAndEmptyArrays) {
    RequestParser parser;
    EXPECT_EQ(parse_all(parser, "\r\n*0\r\n  \r\nPING\r\n"), (std::vector<Args>{{"PING"}}));
}

TEST(RequestParserTest, ParsesPipelinedCommands) {
    RequestParser parser;
    EXPECT_EQ(parse_all(parser, "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$1\r\nk\r\nPING\r\n"),
              (std::vector<Args>{{"PING"}, {"GET", "k"}, {"PING"}}));
}

TEST(RequestParserTest, BulkStringsAreBinarySafe) {
    RequestParser parser;
    const std::string value("a\r\nb\0c", 6);
    const auto commands = parse_all(parser, "*2\r\n$3\r\nSET\r\n$6\r\n" + value + "\r\n");
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0][1], value);
}

// Every possible split point must produce the same result as parsing in one piece.
TEST(RequestParserTest, HandlesArbitraryFragmentation) {
    const std::string wire = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$10\r\n0123456789\r\nGET key\r\n";
    for (std::size_t chunk = 1; chunk <= wire.size(); ++chunk) {
        RequestParser parser;
        std::string buffer;
        std::vector<Args> commands;
        Views views;
        for (std::size_t pos = 0; pos < wire.size(); pos += chunk) {
            buffer += wire.substr(pos, chunk);
            while (true) {
                const auto [status, consumed] = parser.parse(buffer, views);
                ASSERT_NE(status, Status::kProtocolError) << parser.error();
                if (status == Status::kCommandReady) {
                    commands.emplace_back(views.begin(), views.end());
                }
                buffer.erase(0, consumed);
                if (status == Status::kNeedMoreData) {
                    break;
                }
            }
        }
        EXPECT_EQ(commands, (std::vector<Args>{{"SET", "key", "0123456789"}, {"GET", "key"}}))
            << "chunk size " << chunk;
        EXPECT_TRUE(buffer.empty());
    }
}

// A request that is still incomplete is never consumed, so the views returned once it completes
// all point into the caller's (possibly reallocated) buffer.
TEST(RequestParserTest, IncompleteRequestIsNotConsumed) {
    RequestParser parser;
    Views views;
    std::string buffer = "*2\r\n$3\r\nGET\r\n$5\r\nhel";
    const auto partial = parser.parse(buffer, views);
    EXPECT_EQ(partial.status, Status::kNeedMoreData);
    EXPECT_EQ(partial.consumed, 0U);

    buffer += "lo\r\n";
    buffer.reserve(buffer.capacity() * 4);  // force reallocation
    const auto done = parser.parse(buffer, views);
    ASSERT_EQ(done.status, Status::kCommandReady);
    EXPECT_EQ(done.consumed, buffer.size());
    EXPECT_EQ(Args(views.begin(), views.end()), (Args{"GET", "hello"}));
}

TEST(RequestParserTest, RejectsMalformedInput) {
    const std::vector<std::string> bad_inputs = {
        "*x\r\n",                 // non-numeric array length
        "*1\r\n:5\r\n",           // element is not a bulk string
        "*1\r\n$-3\r\n",          // negative bulk length
        "*1\r\n$3\r\nabcde\r\n",  // bulk string longer than declared
        "*99999999999\r\n",       // array too long
    };
    for (const std::string& input : bad_inputs) {
        RequestParser parser;
        Views args;
        EXPECT_EQ(parser.parse(input, args).status, Status::kProtocolError) << input;
        EXPECT_FALSE(parser.error().empty());
    }
}

TEST(RequestParserTest, EnforcesLimits) {
    RequestParser parser({.max_inline_length = 8, .max_bulk_length = 4, .max_array_length = 2});
    Views args;
    EXPECT_EQ(parser.parse("this line never ends", args).status, Status::kProtocolError);
    EXPECT_EQ(RequestParser({.max_bulk_length = 4}).parse("*1\r\n$5\r\n", args).status,
              Status::kProtocolError);
    EXPECT_EQ(RequestParser({.max_array_length = 2}).parse("*3\r\n", args).status,
              Status::kProtocolError);
}

}  // namespace
}  // namespace memkv::protocol
