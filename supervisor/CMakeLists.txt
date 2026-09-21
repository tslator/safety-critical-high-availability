add_library(safety_crit_supervisor STATIC
    src/supervisor.cpp
)
add_library(safety_crit::supervisor ALIAS safety_crit_supervisor)

target_include_directories(safety_crit_supervisor
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(safety_crit_supervisor PUBLIC cxx_std_20)
target_link_libraries(safety_crit_supervisor
    PUBLIC safety_crit::shared_memory
           safety_crit::runtime
           safety_crit::workers
           safety_crit::monitors
)
safety_crit_apply_warnings(safety_crit_supervisor)
safety_crit_apply_sanitizers(safety_crit_supervisor)

if(SAFETY_CRIT_BUILD_TESTING)
    add_subdirectory(tests)
endif()
