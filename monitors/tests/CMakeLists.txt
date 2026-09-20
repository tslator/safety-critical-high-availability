# Phase 3, T3.1: monitors health core tests.
add_executable(monitors_health_test monitors_health_test.cpp)
target_link_libraries(monitors_health_test PRIVATE safety_crit::monitors)
safety_crit_apply_warnings(monitors_health_test)
safety_crit_apply_sanitizers(monitors_health_test)
safety_crit_register_tests(monitors_health_test)

# T3.2: poll loop, liveness verdicts, JSON lines.
add_executable(monitors_loop_test monitors_loop_test.cpp)
target_link_libraries(monitors_loop_test PRIVATE safety_crit::monitors)
safety_crit_apply_warnings(monitors_loop_test)
safety_crit_apply_sanitizers(monitors_loop_test)
safety_crit_register_tests(monitors_loop_test)

# T3.3: fork-based monitor integration (plain build under sanitizers).
add_executable(monitors_integration_test monitors_integration_test.cpp)
target_link_libraries(monitors_integration_test PRIVATE safety_crit::monitors
                                                       safety_crit::workers)
safety_crit_apply_warnings(monitors_integration_test)
safety_crit_apply_sanitizers(monitors_integration_test)
safety_crit_register_tests(monitors_integration_test)
