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
