# Library: safety_crit::shared_memory (Phase 1)
add_library(safety_crit_shared_memory STATIC
    src/integrity.cpp
    src/shared_region.cpp
    src/shm_attach.cpp
)
add_library(safety_crit::shared_memory ALIAS safety_crit_shared_memory)

target_include_directories(safety_crit_shared_memory
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_shared_memory PUBLIC cxx_std_20)

# Clang's codegen does not inline std::atomic<T>::is_lock_free() the way GCC's
# does, so an out-of-line reference to __atomic_is_lock_free survives and the
# link fails without libatomic (found by the clang-verify pipeline, T-0008).
# GCC inlines it, so the gcc-primary baseline link line stays unchanged.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_libraries(safety_crit_shared_memory PUBLIC atomic)
endif()

safety_crit_apply_warnings(safety_crit_shared_memory)
safety_crit_apply_sanitizers(safety_crit_shared_memory)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
