// memkv-server: wires the layers together and runs the event loop.

#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>

#include "memkv/common/clock.hpp"
#include "memkv/common/log.hpp"
#include "memkv/net/server.hpp"
#include "memkv/storage/database.hpp"
#include "server_options.hpp"

namespace {

// Signal handlers cannot capture state, so the running server is reachable through a global.
memkv::net::Server* g_server = nullptr;

extern "C" void handle_shutdown_signal(int /*signal*/) {
    if (g_server != nullptr) {
        g_server->stop();  // only sets an atomic flag, which is safe inside a signal handler
    }
}

}  // namespace

int main(int argc, char** argv) {
    const memkv::app::ServerOptions options = memkv::app::parse_command_line(argc, argv);
    try {
        const memkv::SteadyClock clock;
        memkv::storage::Database db(clock, options.keyspace);
        memkv::net::Server server(options.server, db);

        g_server = &server;
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);

        memkv::log::info("memkv listening on " + options.server.host + ":" +
                         std::to_string(server.port()));
        server.run();
        g_server = nullptr;
        memkv::log::info("shutdown complete");
    } catch (const std::exception& error) {
        memkv::log::error(error.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
