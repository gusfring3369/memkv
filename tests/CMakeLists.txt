include(FetchContent)

# Prefer an installed GoogleTest; otherwise download a pinned release.
find_package(GTest 1.14 CONFIG QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(memkv_tests
  intrusive_list_test.cpp
  skiplist_test.cpp
  sorted_set_test.cpp
  keyspace_test.cpp
  database_test.cpp
  request_parser_test.cpp
  reply_test.cpp
  dispatcher_test.cpp
  server_test.cpp)
target_link_libraries(memkv_tests PRIVATE memkv_net GTest::gtest_main)
gtest_discover_tests(memkv_tests DISCOVERY_TIMEOUT 30)
