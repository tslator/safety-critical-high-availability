# Phase 2, T2.1: workers core tests.
add_executable(workers_core_test workers_core_test.cpp)
target_link_libraries(workers_core_test PRIVATE safety_crit::workers)
safety_crit_apply_warnings(workers_core_test)
safety_crit_apply_sanitizers(workers_core_test)
safety_crit_register_tests(workers_core_test)

# T2.2: work loop, signals, deadline monitoring.
add_executable(workers_loop_test workers_loop_test.cpp)
target_link_libraries(workers_loop_test PRIVATE safety_crit::workers)
safety_crit_apply_warnings(workers_loop_test)
safety_crit_apply_sanitizers(workers_loop_test)
safety_crit_register_tests(workers_loop_test)
