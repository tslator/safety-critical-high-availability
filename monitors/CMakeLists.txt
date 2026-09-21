# Phase 3, T3.1: monitors library (DEC-0010 #6).
add_library(safety_crit_monitors STATIC
    src/monitor_config.cpp
    src/pidfile_liveness.cpp
    src/json_lines.cpp
    src/monitor_entry.cpp
)
add_library(safety_crit::monitors ALIAS safety_crit_monitors)

target_include_directories(safety_crit_monitors
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_monitors PUBLIC cxx_std_20)
target_link_libraries(safety_crit_monitors
    PUBLIC safety_crit::shared_memory
           safety_crit::runtime
    PRIVATE safety_crit::workers  # reuses the T2.2 signal wiring (DEC-0009 #3)
)

safety_crit_apply_warnings(safety_crit_monitors)
safety_crit_apply_sanitizers(safety_crit_monitors)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
