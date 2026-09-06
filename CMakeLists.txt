cmake_minimum_required(VERSION 3.15)
project(kvstore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kvstore_server src/server.cpp)
target_include_directories(kvstore_server PRIVATE include)

if(WIN32)
    target_link_libraries(kvstore_server PRIVATE ws2_32)
else()
    target_link_libraries(kvstore_server PRIVATE pthread)
endif()

add_executable(kvstore_client src/client.cpp)
target_include_directories(kvstore_client PRIVATE include)

if(WIN32)
    target_link_libraries(kvstore_client PRIVATE ws2_32)
else()
    target_link_libraries(kvstore_client PRIVATE pthread)
endif()
# ---- Tests (GTest via FetchContent) ----
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# Windows: force GTest to use the same dynamic/static runtime as our own build
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(kvstore_tests tests/test_kvstore.cpp)
target_include_directories(kvstore_tests PRIVATE include)
target_link_libraries(kvstore_tests PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(kvstore_tests)