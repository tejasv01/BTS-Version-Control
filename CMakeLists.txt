cmake_minimum_required(VERSION 3.16)
project(bts VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src/include)

add_library(bts_lib STATIC
    src/core/object_store_stub.cpp
    src/core/index_stub.cpp
    src/core/refs.cpp
    src/commands/branch.cpp
    src/commands/checkout.cpp
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

enable_testing()

function(bts_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        bts_lib
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

bts_add_test(test_refs     tests/test_refs.cpp)
bts_add_test(test_branch   tests/test_branch.cpp)
bts_add_test(test_checkout tests/test_checkout.cpp)
