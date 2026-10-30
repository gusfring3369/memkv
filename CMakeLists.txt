cmake_minimum_required(VERSION 3.20)
project(memkv VERSION 1.0.0 DESCRIPTION "Redis-compatible in-memory key-value store" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MEMKV_BUILD_TESTS "Build the unit and integration tests" ON)
option(MEMKV_BUILD_BENCHMARKS "Build the benchmarks" ON)
option(MEMKV_SANITIZE "Instrument with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MEMKV_WERROR "Treat compiler warnings as errors" OFF)
option(MEMKV_FORCE_POLL "Use the portable poll(2) backend even where epoll is available" OFF)

# ---- Shared compiler settings -------------------------------------------------------------------
add_library(memkv_options INTERFACE)
target_compile_options(memkv_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference
  # Designated initializers that rely on default member values are intentional (GCC < 14 warns).
  -Wno-missing-field-initializers)
if(MEMKV_WERROR)
  target_compile_options(memkv_options INTERFACE -Werror)
endif()
if(MEMKV_SANITIZE)
  target_compile_options(memkv_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(memkv_options INTERFACE -fsanitize=address,undefined)
endif()

# ---- Libraries, one per layer -------------------------------------------------------------------
# Each layer may only depend on the layers below it; the link graph enforces that:
#   net -> command -> {storage, protocol} -> ds -> common
add_library(memkv_common src/memkv/common/log.cpp)
target_include_directories(memkv_common PUBLIC src)
target_link_libraries(memkv_common PUBLIC memkv_options)

add_library(memkv_storage
  src/memkv/ds/skiplist.cpp
  src/memkv/storage/sorted_set.cpp
  src/memkv/storage/keyspace.cpp
  src/memkv/storage/database.cpp)
target_link_libraries(memkv_storage PUBLIC memkv_common)

add_library(memkv_protocol
  src/memkv/protocol/reply.cpp
  src/memkv/protocol/request_parser.cpp)
target_link_libraries(memkv_protocol PUBLIC memkv_common)

add_library(memkv_command src/memkv/command/dispatcher.cpp)
target_link_libraries(memkv_command PUBLIC memkv_storage memkv_protocol)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MEMKV_FORCE_POLL)
  set(MEMKV_POLLER_SOURCE src/memkv/net/poller_epoll.cpp)
else()
  set(MEMKV_POLLER_SOURCE src/memkv/net/poller_poll.cpp)
endif()

add_library(memkv_net
  src/memkv/net/socket.cpp
  src/memkv/net/connection.cpp
  src/memkv/net/server.cpp
  ${MEMKV_POLLER_SOURCE})
target_link_libraries(memkv_net PUBLIC memkv_command)

# ---- Executables --------------------------------------------------------------------------------
add_executable(memkv-server apps/memkv_server.cpp)
target_link_libraries(memkv-server PRIVATE memkv_net)

if(MEMKV_BUILD_BENCHMARKS)
  add_executable(memkv-bench bench/storage_bench.cpp)
  target_link_libraries(memkv-bench PRIVATE memkv_storage)
endif()

if(MEMKV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
