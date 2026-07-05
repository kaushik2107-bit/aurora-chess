cmake_minimum_required(VERSION 3.24)
project(aurora_chess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(AURORA_ENABLE_NATIVE_ARCH "Tune release builds for the build machine CPU" ON)
option(AURORA_ENABLE_LTO "Enable interprocedural optimization for release builds when supported" ON)

include(CheckIPOSupported)
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# For Windows: prevent overriding the compiler runtime library settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(configure_aurora_target target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/build/bin/debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/build/bin/release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/build/bin/release
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}/build/bin/release
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/release
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/release
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/release
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/debug
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/release
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/release
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/release
    )
endfunction()

function(configure_aurora_pext_target target_name)
    if (NOT MSVC)
        target_compile_options(${target_name} PRIVATE -mbmi2)
    endif()
endfunction()

function(configure_aurora_optimized_target target_name)
    target_compile_options(${target_name} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/O2>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/Ob3>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/Oi>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/Ot>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/GL>
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release,RelWithDebInfo>>:/arch:AVX2>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release,RelWithDebInfo>>:-O3>
    )

    if (AURORA_ENABLE_NATIVE_ARCH AND NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release,RelWithDebInfo>:-march=native>
            $<$<CONFIG:Release,RelWithDebInfo>:-mtune=native>
        )
    endif()

    if (AURORA_ENABLE_LTO)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
        if (ipo_supported)
            set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        else()
            message(STATUS "IPO/LTO not enabled for ${target_name}: ${ipo_error}")
        endif()
    endif()
endfunction()

add_library(aurora_chess STATIC
    src/engine.cpp
    src/uci.cpp
    src/ucioptions.cpp
    src/attacks.cpp
    src/board.cpp
    src/evaluation.cpp
    src/movegen.cpp
    src/movepick.cpp
    src/nnue.cpp
    src/perft.cpp
    src/search.cpp
    src/see.cpp
    src/speed.cpp
    src/thread.cpp
    src/ttable.cpp
    src/zobrist.cpp
)

target_include_directories(aurora_chess PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(aurora_chess PUBLIC cxx_std_20)
configure_aurora_target(aurora_chess)
configure_aurora_pext_target(aurora_chess)
configure_aurora_optimized_target(aurora_chess)

add_executable(aurora_chess_app
    src/main.cpp
)

target_link_libraries(aurora_chess_app PRIVATE aurora_chess)
configure_aurora_target(aurora_chess_app)
configure_aurora_pext_target(aurora_chess_app)
configure_aurora_optimized_target(aurora_chess_app)

enable_testing()
include(GoogleTest)

add_executable(aurora_chess_tests
    tests/test_engine.cpp
    tests/test_board.cpp
    tests/test_evaluation.cpp
    tests/test_pext.cpp
    tests/test_movegen.cpp
    tests/test_movepick.cpp
    tests/test_nnue.cpp
    tests/test_perft.cpp
    tests/test_search.cpp
    tests/test_see.cpp
    tests/test_speed.cpp
    tests/test_ttable.cpp
    tests/test_uci.cpp
    tests/test_ucioptions.cpp
)

target_link_libraries(aurora_chess_tests PRIVATE aurora_chess GTest::gtest_main)
configure_aurora_target(aurora_chess_tests)
configure_aurora_pext_target(aurora_chess_tests)
configure_aurora_optimized_target(aurora_chess_tests)
gtest_discover_tests(aurora_chess_tests)
