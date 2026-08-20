if(SAFETY_CRIT_ENABLE_TSAN
   AND (SAFETY_CRIT_ENABLE_ASAN OR SAFETY_CRIT_ENABLE_UBSAN))
    message(FATAL_ERROR
        "ThreadSanitizer cannot be combined with AddressSanitizer or "
        "UndefinedBehaviorSanitizer. Enable TSan separately.")
endif()

function(safety_crit_apply_sanitizers target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "safety_crit_apply_sanitizers: target '${target_name}' does not exist.")
    endif()

    if(NOT SAFETY_CRIT_ENABLE_ASAN
       AND NOT SAFETY_CRIT_ENABLE_UBSAN
       AND NOT SAFETY_CRIT_ENABLE_TSAN)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "Sanitizers are enabled, but compiler '${CMAKE_CXX_COMPILER_ID}' "
            "is not supported by this Phase 0 configuration.")
    endif()

    set(sanitizer_flags)

    if(SAFETY_CRIT_ENABLE_ASAN)
        list(APPEND sanitizer_flags "-fsanitize=address")
    endif()

    if(SAFETY_CRIT_ENABLE_UBSAN)
        list(APPEND sanitizer_flags "-fsanitize=undefined")
    endif()

    if(SAFETY_CRIT_ENABLE_TSAN)
        list(APPEND sanitizer_flags "-fsanitize=thread")
    endif()

    target_compile_options(${target_name} PRIVATE ${sanitizer_flags})
    target_link_options(${target_name} PRIVATE ${sanitizer_flags})
endfunction()
