cmake_minimum_required(VERSION 3.16)
project(minefolio VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    csilk
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../deps/csilk"
    GIT_REPOSITORY https://github.com/quintin-lee/csilk.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(csilk)

# Find libpq for PostgreSQL support
find_package(PkgConfig QUIET)
pkg_check_modules(PQ libpq)

# Minefolio sources (recursive glob for all subdirectories)
file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(minefolio ${SOURCES})

target_include_directories(minefolio PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(minefolio PRIVATE _GNU_SOURCE)
target_compile_options(minefolio PRIVATE -Wall -Wextra -Wno-unused-parameter)

find_package(CURL REQUIRED)

target_link_libraries(minefolio PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})

if(PQ_INCLUDE_DIRS)
    target_include_directories(minefolio PRIVATE ${PQ_INCLUDE_DIRS})
endif()

# ---------------------------------------------------------------------------
# Pre-build auto-format: keep src/**/*.{c,h} conformant to .clang-format
# Requires clang-format >= 14 (.clang-format uses AlignTrailingComments Kind:)
# Third-party / generated code (build/, deps/, _deps/, vendor/, third_party/)
# is explicitly excluded so the hook never touches external libraries.
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE
    NAMES clang-format clang-format-18 clang-format-17 clang-format-16
          clang-format-15 clang-format-14
)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES "src/*.c" "src/*.h")
    # Hard guarantee: never format third-party or generated code
    list(FILTER FORMAT_SOURCES EXCLUDE REGEX "(/|^)(build|deps|_deps|vendor|third[_-]?party)/")
    if(FORMAT_SOURCES)
        set(FORMAT_STAMP "${CMAKE_BINARY_DIR}/clang-format.stamp")
        add_custom_command(
            OUTPUT "${FORMAT_STAMP}"
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT="${CLANG_FORMAT_EXE}"
                "-DSOURCES=${FORMAT_SOURCES}"
                -DSTAMP="${FORMAT_STAMP}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormatSources.cmake"
            DEPENDS ${FORMAT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format"
            COMMENT "Checking source formatting"
            VERBATIM
        )
        add_custom_target(format_sources ALL DEPENDS "${FORMAT_STAMP}")
        add_dependencies(minefolio format_sources)
    else()
        message(STATUS "no sources to format - skipping pre-build auto-format")
    endif()
else()
    message(STATUS "clang-format not found - skipping pre-build auto-format")
endif()

# Copy config and sql on build
add_custom_command(TARGET minefolio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_BINARY_DIR}/config"
)
add_custom_command(TARGET minefolio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sql"
        "${CMAKE_BINARY_DIR}/sql"
)
if(DEFINED csilk_SOURCE_DIR)
    add_custom_command(TARGET minefolio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${csilk_SOURCE_DIR}/share"
            "${CMAKE_BINARY_DIR}/share"
    )
endif()

# ==========================================
# Unit Tests (Financial Core)
# ==========================================
enable_testing()

set(FINANCIAL_CORE_SOURCES
    src/core/financial/currency.c
    src/core/financial/decimal.c
    src/core/financial/money.c
    src/core/financial/quantity.c
    src/core/financial/price.c
    src/core/financial/rate.c
    src/core/financial/percentage.c
)

function(add_financial_unit_test test_target test_source)
    add_executable(${test_target}
        ${test_source}
        ${FINANCIAL_CORE_SOURCES}
        src/core/ledger/ledger_engine.c
        src/common/balance.c
        src/common/tx_types.c
        src/common/db.c
        src/common/config.c
        src/repositories/transaction_repo.c
    )
    target_include_directories(${test_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${test_target} PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
    add_test(NAME ${test_target} COMMAND ${test_target})
endfunction()

add_financial_unit_test(test_currency tests/unit/test_currency.c)
add_financial_unit_test(test_decimal tests/unit/test_decimal.c)
add_financial_unit_test(test_money tests/unit/test_money.c)
add_financial_unit_test(test_quantity tests/unit/test_quantity.c)
add_financial_unit_test(test_price tests/unit/test_price.c)
add_financial_unit_test(test_rate tests/unit/test_rate.c)
add_financial_unit_test(test_pnl tests/unit/test_pnl.c)
add_financial_unit_test(test_fx tests/unit/test_fx.c)
add_financial_unit_test(test_ledger_math tests/unit/test_ledger_math.c)
add_financial_unit_test(test_ledger_engine tests/unit/test_ledger_engine.c)

set(LIB_SOURCES ${SOURCES})
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main.c$")

add_executable(test_ai_tools tests/unit/test_ai_tools.c ${LIB_SOURCES})
target_include_directories(test_ai_tools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_ai_tools PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
add_test(NAME test_ai_tools COMMAND test_ai_tools)



