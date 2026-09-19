# clang-verify toolchain (DEC-0008): supplementary Clang verification build.
# Intended for the digest-pinned verification container
# (containers/verification/Dockerfile.clang, tag safety-critical-ha:verify-clang-14),
# which provides Debian bookworm's clang-14 with the same libc/libstdc++ as the
# gcc-primary baseline, isolating compiler differences.
#
# Policy (DEC-0008 #4): this file sets ONLY the compilers and the C++20 cache
# variables. No add_compile_options here - the warning policy stays in
# CMake/Warnings.cmake (per-target via safety_crit_apply_warnings), so
# FetchContent'd GTest/Catch2 headers are never warned on and flags are never
# double-applied.

set(CMAKE_C_COMPILER   clang-14 CACHE FILEPATH "C compiler (clang-verify)")
set(CMAKE_CXX_COMPILER clang++-14 CACHE FILEPATH "C++ compiler (clang-verify)")

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard (clang-verify)")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require C++20 (clang-verify)")
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "No GNU extensions (clang-verify)")

# Leave sanitizer coverage alone; clang-sanitizers presets are deferred
# (DEC-0008 #3) and CMake/Sanitizers.cmake already supports Clang|GNU.
