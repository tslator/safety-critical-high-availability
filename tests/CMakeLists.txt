include(TestFramework)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framework.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/test_framework.hpp
    @ONLY
)

add_executable(smoke_test
    smoke_test.cpp
)

target_include_directories(smoke_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "GoogleTest")
    target_compile_definitions(smoke_test PRIVATE SAFETY_CRIT_USE_GTEST)
elseif(SAFETY_CRIT_TEST_FRAMEWORK STREQUAL "Catch2")
    target_compile_definitions(smoke_test PRIVATE SAFETY_CRIT_USE_CATCH2)
endif()

safety_crit_register_tests(smoke_test)
