# Library: safety_crit::shared_memory (Phase 1)
add_library(safety_crit_shared_memory STATIC
    src/shared_region.cpp
)
add_library(safety_crit::shared_memory ALIAS safety_crit_shared_memory)

target_include_directories(safety_crit_shared_memory
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_shared_memory PUBLIC cxx_std_20)

safety_crit_apply_warnings(safety_crit_shared_memory)
safety_crit_apply_sanitizers(safety_crit_shared_memory)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
