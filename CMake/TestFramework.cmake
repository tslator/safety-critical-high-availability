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

elseif(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "Catch2")
    FetchContent_Declare(
        catch2
        URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.3.tar.gz
        URL_HASH SHA256=b0299ae552918220a7a6e21e7de5b714777f4e8c883fb70c4bb23fe01df8c6e3
    )
    FetchContent_MakeAvailable(catch2)

    # catch_discover_tests is provided by Catch.cmake in the extras directory.
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

function(safety_crit_register_tests target_name)
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