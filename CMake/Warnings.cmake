# Project-owned warning policy (Phase 0 contract, Task 0.2 step 4).
# Apply to first-party targets only; never to third-party/framework code.
option(SAFETY_CRIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(safety_crit_apply_warnings target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "safety_crit_apply_warnings: target '${target_name}' does not exist.")
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()

    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )

    if(SAFETY_CRIT_WARNINGS_AS_ERRORS)
        target_compile_options(${target_name} PRIVATE -Werror)
    endif()
endfunction()
