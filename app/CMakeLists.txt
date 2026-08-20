add_executable(safety-critical-ha
    src/main.cpp
)

target_compile_features(safety-critical-ha PRIVATE cxx_std_20)

target_compile_definitions(safety-critical-ha
    PRIVATE
    SAFETY_CRIT_COMPILER_NAME="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)