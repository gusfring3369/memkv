#pragma once

#include "memkv/net/server.hpp"
#include "memkv/storage/keyspace.hpp"

namespace memkv::app {

/// Everything configurable from the memkv-server command line.
struct ServerOptions {
    net::ServerConfig server;
    storage::KeyspaceConfig keyspace;
};

/// Parses argv. Prints usage and exits on --help or on an invalid option.
[[nodiscard]] ServerOptions parse_command_line(int argc, char** argv);

}  // namespace memkv::app
