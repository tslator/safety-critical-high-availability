# Phase 1, T1.1: shared-memory unit tests.
add_executable(atomic_flags_test atomic_flags_test.cpp)
target_link_libraries(atomic_flags_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(atomic_flags_test)
safety_crit_apply_sanitizers(atomic_flags_test)
safety_crit_register_tests(atomic_flags_test)

add_executable(shared_region_layout_test shared_region_layout_test.cpp)
target_link_libraries(shared_region_layout_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(shared_region_layout_test)
safety_crit_apply_sanitizers(shared_region_layout_test)
safety_crit_register_tests(shared_region_layout_test)

# T1.2: lock-free ring buffer (protocol, ordering, MPMC accounting, recovery).
add_executable(ring_buffer_test ring_buffer_test.cpp)
target_link_libraries(ring_buffer_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(ring_buffer_test)
safety_crit_apply_sanitizers(ring_buffer_test)
safety_crit_register_tests(ring_buffer_test)

# T1.5: stress tests (>= 1M ops, zero loss/duplication accounting). The
# SAFETY_CRIT_STRESS_OPS cache variable overrides the built-in 1M default for
# constrained environments only (all standard matrix legs, TSan included, run
# the full 1M; deviation #12 in docs/phases/PHASE_1_SHARED_MEMORY.md).
add_executable(ring_buffer_stress_test ring_buffer_stress.cpp)
target_link_libraries(ring_buffer_stress_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(ring_buffer_stress_test)
safety_crit_apply_sanitizers(ring_buffer_stress_test)
if(SAFETY_CRIT_STRESS_OPS)
    target_compile_definitions(ring_buffer_stress_test PRIVATE
        SAFETY_CRIT_STRESS_OPS=${SAFETY_CRIT_STRESS_OPS})
endif()
safety_crit_register_tests(ring_buffer_stress_test)

# T1.3: CRC integrity (known vectors, corruption skip path, region word).
add_executable(integrity_test integrity_test.cpp)
target_link_libraries(integrity_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(integrity_test)
safety_crit_apply_sanitizers(integrity_test)
safety_crit_register_tests(integrity_test)

# T1.4: named shared-memory attach/detach (create-vs-existing, stale rejection,
# cross-process re-attach). Suite name is lowercase `shm_attach` so the G1.4
# gate command `ctest -R shm` matches the registered (case-sensitive) test names.
add_executable(shm_attach_test shm_attach_test.cpp)
target_link_libraries(shm_attach_test PRIVATE safety_crit::shared_memory)
safety_crit_apply_warnings(shm_attach_test)
safety_crit_apply_sanitizers(shm_attach_test)
safety_crit_register_tests(shm_attach_test)
