add_library(safety_crit_runtime STATIC
    src/scheduling.cpp
)
add_library(safety_crit::runtime ALIAS safety_crit_runtime)

target_include_directories(safety_crit_runtime
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_runtime PUBLIC cxx_std_20)
safety_crit_apply_warnings(safety_crit_runtime)
safety_crit_apply_sanitizers(safety_crit_runtime)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
