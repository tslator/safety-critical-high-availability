include(TestFramework)

add_executable(smoke_test
    smoke_test.cpp
)

safety_crit_apply_warnings(smoke_test)
safety_crit_register_tests(smoke_test)
