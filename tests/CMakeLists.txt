include(TestFramework)

add_executable(smoke_test
    smoke_test.cpp
)

target_compile_features(smoke_test PRIVATE cxx_std_20)

safety_crit_register_tests(smoke_test)
