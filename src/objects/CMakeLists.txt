cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(bts_objects CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT TARGET bts_storage)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../storage ${CMAKE_CURRENT_BINARY_DIR}/bts_storage_build)
    endif()
endif()

add_library(bts_objects
    blob.cpp
    tree.cpp
    commit.cpp
)

target_include_directories(bts_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(bts_objects PUBLIC
    bts_storage
)

target_compile_features(bts_objects PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bts_objects PRIVATE -Wall -Wextra)
endif()
