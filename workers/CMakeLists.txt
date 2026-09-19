# Phase 2, T2.1: workers library (DEC-0009).
add_library(safety_crit_workers STATIC
    src/worker_config.cpp
    src/signals.cpp
)
add_library(safety_crit::workers ALIAS safety_crit_workers)

target_include_directories(safety_crit_workers
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_workers PUBLIC cxx_std_20)
target_link_libraries(safety_crit_workers PUBLIC safety_crit::shared_memory)

safety_crit_apply_warnings(safety_crit_workers)
safety_crit_apply_sanitizers(safety_crit_workers)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
