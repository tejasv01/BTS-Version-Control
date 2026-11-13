cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(bts_storage CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(bts_storage
    hash.cpp
    compress.cpp
    object_store.cpp
)

target_include_directories(bts_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(bts_storage PUBLIC
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_features(bts_storage PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bts_storage PRIVATE -Wall -Wextra)
endif()
