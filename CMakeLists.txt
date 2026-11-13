cmake_minimum_required(VERSION 3.16)
project(bts VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/opt/homebrew/opt/openssl@3")
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src/include)

add_library(bts_lib STATIC
    src/storage/hash.cpp
    src/storage/compress.cpp
    src/core/object_store_stub.cpp
    src/core/index.cpp
    src/core/diff.cpp
    src/core/refs.cpp
    src/commands/branch.cpp
    src/commands/checkout.cpp
    src/commands/log.cpp
    src/commands/cli.cpp
)

target_include_directories(bts_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(bts_lib PUBLIC
    OpenSSL::Crypto
    ZLIB::ZLIB
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(bts_lib PUBLIC stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(bts_lib PUBLIC c++fs)
endif()

add_executable(bts src/main.cpp)
target_link_libraries(bts PRIVATE bts_lib)

install(TARGETS bts DESTINATION bin)

enable_testing()
include(GoogleTest)

function(bts_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        bts_lib
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

bts_add_test(test_refs               tests/test_refs.cpp)
bts_add_test(test_branch             tests/test_branch.cpp)
bts_add_test(test_checkout           tests/test_checkout.cpp)
bts_add_test(test_log                tests/test_log.cpp)
bts_add_test(test_cli                tests/test_cli.cpp)
bts_add_test(test_index              tests/test_index.cpp)
bts_add_test(test_diff               tests/test_diff.cpp)
bts_add_test(test_add_status_commit  tests/test_add_status_commit.cpp)
bts_add_test(test_system_e2e         tests/test_system_e2e.cpp)

add_subdirectory(src/storage)
add_subdirectory(src/objects)
add_subdirectory(tests/storage)
add_subdirectory(tests/objects)
