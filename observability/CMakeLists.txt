add_library(safety_crit_observability STATIC
    src/event_log.cpp
)
add_library(safety_crit::observability ALIAS safety_crit_observability)

target_include_directories(safety_crit_observability
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_observability PUBLIC cxx_std_20)
safety_crit_apply_warnings(safety_crit_observability)
safety_crit_apply_sanitizers(safety_crit_observability)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
