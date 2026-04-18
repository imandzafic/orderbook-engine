cmake_minimum_required(VERSION 3.16)
project(orderbook_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Release by default ───────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Aggressive optimisations for benchmarking ────────────────────────
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -flto")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined")

# ── Include paths ────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Main executable ──────────────────────────────────────────────────
add_executable(orderbook src/main.cpp)

# ── Thread support (for steady_clock and future multi-threaded work) ─
find_package(Threads REQUIRED)
target_link_libraries(orderbook Threads::Threads)
