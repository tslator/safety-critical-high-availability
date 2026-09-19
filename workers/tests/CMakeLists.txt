# Phase 2, T2.1: workers core tests.
add_executable(workers_core_test workers_core_test.cpp)
target_link_libraries(workers_core_test PRIVATE safety_crit::workers)
safety_crit_apply_warnings(workers_core_test)
safety_crit_apply_sanitizers(workers_core_test)
safety_crit_register_tests(workers_core_test)
