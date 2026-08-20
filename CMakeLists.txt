cmake_minimum_required(VERSION 3.24)
project(safety-critical-ha VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


option(SAFETY_CRIT_BUILD_TESTING "Build tests" ON)
set(SAFETY_CRIT_TEST_FRAMEWORK "GoogleTest" CACHE STRING
    "Test framework: GoogleTest or Catch2")
set_property(CACHE SAFETY_CRIT_TEST_FRAMEWORK PROPERTY STRINGS GoogleTest Catch2)
option(SAFETY_CRIT_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SAFETY_CRIT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SAFETY_CRIT_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

add_subdirectory(app)
