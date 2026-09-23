add_library(safety_crit_perturb STATIC
    src/harness.cpp
)
add_library(safety_crit::perturb ALIAS safety_crit_perturb)

target_include_directories(safety_crit_perturb
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_perturb PUBLIC cxx_std_20)
safety_crit_apply_warnings(safety_crit_perturb)
safety_crit_apply_sanitizers(safety_crit_perturb)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
