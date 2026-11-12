cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(bts_storage_tests CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    enable_testing()
    if(NOT TARGET bts_storage)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../src/storage ${CMAKE_CURRENT_BINARY_DIR}/bts_storage_build)
    endif()
endif()

find_package(GTest REQUIRED)

add_executable(storage_tests
    test_hash.cpp
    test_compress.cpp
    test_object_store.cpp
)

target_link_libraries(storage_tests PRIVATE
    bts_storage
    GTest::gtest_main
)

target_compile_features(storage_tests PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(storage_tests PRIVATE -Wall -Wextra)
endif()

include(GoogleTest)
gtest_discover_tests(storage_tests)
