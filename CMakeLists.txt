cmake_minimum_required(VERSION 3.24)
project(aurora_chess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# For Windows: prevent overriding the compiler runtime library settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(aurora_chess STATIC
    src/engine.cpp
    src/board.cpp
    src/movegen.cpp
    src/magic.cpp
)

target_include_directories(aurora_chess PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(aurora_chess PUBLIC cxx_std_20)
if (MSVC)
    target_compile_options(aurora_chess PRIVATE /arch:AVX2 /O2)
else()
    target_compile_options(aurora_chess PRIVATE -O3 -march=native)
endif()

add_executable(aurora_chess_app
    src/main.cpp
)

target_link_libraries(aurora_chess_app PRIVATE aurora_chess)

enable_testing()
include(GoogleTest)

add_executable(aurora_chess_tests
    tests/test_engine.cpp
    tests/test_board.cpp
    tests/test_magic.cpp
    tests/test_movegen.cpp
    tests/test_perft.cpp
)

target_link_libraries(aurora_chess_tests PRIVATE aurora_chess GTest::gtest_main)
gtest_discover_tests(aurora_chess_tests)
