# Pre-build auto-format worker. Invoked from CMakeLists.txt via:
#   cmake -DCLANG_FORMAT=<path> "-DSOURCES=<space-joined abs paths>" -DSTAMP=<path> -P FormatSources.cmake
#
# Invariant: the SAME clang-format binary and .clang-format config perform both
# the check (--dry-run) and the fix (-i). Do NOT add a --style override on only
# one of the two paths, or fixed output could still fail the check while the
# stamp hides the drift.

separate_arguments(SOURCES)

execute_process(
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${SOURCES}
    RESULT_VARIABLE batch_result
    OUTPUT_QUIET
    ERROR_QUIET
)

if(batch_result EQUAL 0)
    # Fast path: everything already conforms.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}")
    return()
endif()

# Slow path: locate offenders individually; rewrite ONLY those files so
# untouched files keep their mtimes (avoids full-recompile cascades).
# NOTE: a config-parse failure makes EVERY file look like an offender here;
# the first -i then fails with the same config error -> FATAL_ERROR below.
# That chain is intentional. Do not special-case it.
foreach(src IN LISTS SOURCES)
    execute_process(
        COMMAND "${CLANG_FORMAT}" --dry-run --Werror "${src}"
        RESULT_VARIABLE file_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT file_result EQUAL 0)
        execute_process(
            COMMAND "${CLANG_FORMAT}" -i "${src}"
            RESULT_VARIABLE fix_result
        )
        if(NOT fix_result EQUAL 0)
            message(FATAL_ERROR "clang-format failed on ${src}")
        endif()
        message(STATUS "clang-formatted ${src}")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}")
