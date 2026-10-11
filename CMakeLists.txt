cmake_minimum_required(VERSION 3.24)

project(tapedeck
    VERSION 0.1.0
    DESCRIPTION "Low-latency NASDAQ ITCH 5.0 market replay engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TAPEDECK_BUILD_TESTS "Build unit tests" ON)
option(TAPEDECK_BUILD_BENCH "Build micro-benchmarks" ON)
option(TAPEDECK_BUILD_TUI "Build the terminal UI player" ON)
set(TAPEDECK_SANITIZE "" CACHE STRING "Sanitizer to enable: address, thread, undefined (or empty)")

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Dependencies.cmake)

find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# Core library: decoder, order book, signals, replay engine.
# ---------------------------------------------------------------------------
add_library(tapedeck STATIC
    src/core/format.cpp
    src/itch/source.cpp
    src/itch/frame_stream.cpp
    src/book/order_book.cpp
    src/book/book_manager.cpp
    src/replay/replay_handler.cpp
    src/replay/replayer.cpp
)
add_library(tapedeck::tapedeck ALIAS tapedeck)
target_include_directories(tapedeck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tapedeck PUBLIC ZLIB::ZLIB)
tapedeck_set_warnings(tapedeck)
tapedeck_set_sanitizer(tapedeck)

add_subdirectory(apps)

if(TAPEDECK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TAPEDECK_BUILD_BENCH)
    add_subdirectory(bench)
endif()
