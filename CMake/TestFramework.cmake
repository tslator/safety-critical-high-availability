# Guard against re-inclusion: this module is included both from the top-level
# CMakeLists.txt and from tests/CMakeLists.txt.
include_guard(GLOBAL)

# Directory containing this module. Captured here (at include scope) because
# CMAKE_CURRENT_LIST_DIR inside a function refers to the caller's directory.
set(SAFETY_CRIT_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "GoogleTest"
   AND NOT SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "Catch2")
    message(FATAL_ERROR 
        "Invalid SAFETY_CRIT_TEST_FRAMEWORK='${SAFETY_CRIT_TEST_FRAMEWORK}'. "
        "Supported values are 'GoogleTest' and 'Catch2'.")
endif()

include(FetchContent)


if(FETCHCONTENT_FULLY_DISCONNECTED)
    message(STATUS
        "FETCHCONTENT_FULLY_DISCONNECTED=ON: using pre-populated dependencies only.")
endif()

# Fetch and expose the selected test framework.
if(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "GoogleTest")
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    )
    FetchContent_MakeAvailable(googletest)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest was not made available: target 'GTest::gtest_main' does not exist. "
            "With FETCHCONTENT_FULLY_DISCONNECTED=ON, FetchContent assumes the sources are "
            "already populated in '${googletest_SOURCE_DIR}' and silently skips them when that "
            "directory is missing. Run one normal (online) configure in this build directory "
            "first, or copy 'googletest-src' from an existing populated build's '_deps' "
            "directory, then re-run with -DFETCHCONTENT_FULLY_DISCONNECTED=ON.")
    endif()

elseif(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "Catch2")
    FetchContent_Declare(
        catch2
        URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.3.tar.gz
        URL_HASH SHA256=b0299ae552918220a7a6e21e7de5b714777f4e8c883fb70c4bb23fe01df8c6e3
    )
    FetchContent_MakeAvailable(catch2)

    if(NOT TARGET Catch2::Catch2WithMain)
        message(FATAL_ERROR
            "Catch2 was not made available: target 'Catch2::Catch2WithMain' does not exist. "
            "With FETCHCONTENT_FULLY_DISCONNECTED=ON, FetchContent assumes the sources are "
            "already populated in '${catch2_SOURCE_DIR}' and silently skips them when that "
            "directory is missing. Run one normal (online) configure in this build directory "
            "first, or copy 'catch2-src' from an existing populated build's '_deps' "
            "directory, then re-run with -DFETCHCONTENT_FULLY_DISCONNECTED=ON.")
    endif()

    # catch_discover_tests is provided by Catch.cmake in the extras directory.
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# Generate the framework-agnostic test adapter header (tests/test_framework.hpp.in)
# into output_dir. Test targets add that directory to their include path and
# then #include "test_framework.hpp" for SAFETY_CRIT_TEST_CASE/SAFETY_CRIT_ASSERT.
function(safety_crit_configure_test_adapter output_dir)
    configure_file(
        "${SAFETY_CRIT_CMAKE_DIR}/../tests/test_framework.hpp.in"
        "${output_dir}/test_framework.hpp"
        COPYONLY)
endfunction()

# Prepare a unit-test executable for the selected framework: generates the
# adapter header into the caller's binary dir and adds the include path plus
# the framework selection macro. Called by safety_crit_register_tests.
function(safety_crit_prepare_test target_name)
    safety_crit_configure_test_adapter(${CMAKE_CURRENT_BINARY_DIR})
    if(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "GoogleTest")
        target_compile_definitions(${target_name} PRIVATE SAFETY_CRIT_USE_GTEST)
    else()
        target_compile_definitions(${target_name} PRIVATE SAFETY_CRIT_USE_CATCH2)
    endif()
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

function(safety_crit_register_tests target_name)
    safety_crit_prepare_test(${target_name})

    if(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "GoogleTest")
        target_link_libraries(${target_name} PRIVATE GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(${target_name})

    elseif(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "Catch2")
        target_link_libraries(${target_name} PRIVATE Catch2::Catch2WithMain)
        include(Catch)
        catch_discover_tests(${target_name})
    endif()
endfunction()