cmake_minimum_required(VERSION 3.16)
project(minefolio VERSION 1.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries" FORCE)
set(CSILK_BUILD_SHARED OFF CACHE BOOL "Build shared library (in addition to static)" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_BINARY_DIR}/config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sql"
        "${CMAKE_BINARY_DIR}/sql"
)
add_dependencies(minefolio copy_assets)
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

set(DATABASE_INFRA_SOURCES
    src/infrastructure/database/database.c
    src/infrastructure/database/statement.c
    src/infrastructure/database/transaction.c
    src/infrastructure/database/sqlite/sqlite_adapter.c
    src/infrastructure/database/postgres/postgres_adapter.c
    src/infrastructure/database/migration/checksum.c
    src/infrastructure/database/migration/lock.c
    src/infrastructure/database/migration/migration_engine.c
)

function(add_financial_unit_test test_target test_source)
    add_executable(${test_target}
        ${test_source}
        ${FINANCIAL_CORE_SOURCES}
        ${DATABASE_INFRA_SOURCES}
        src/core/ledger/ledger_engine.c
        src/common/balance.c
        src/common/tx_types.c
        src/common/db.c
        src/common/config.c
        src/config/secret.c
        src/repositories/transaction_repo.c
    )
    target_include_directories(${test_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
    target_link_libraries(${test_target} PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
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
target_compile_options(test_ai_tools PRIVATE -UNDEBUG)
target_link_libraries(test_ai_tools PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
add_test(NAME test_ai_tools COMMAND test_ai_tools)

add_executable(test_ai_policy tests/unit/test_ai_policy.c ${LIB_SOURCES})
target_include_directories(test_ai_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ai_policy PRIVATE -UNDEBUG)
target_link_libraries(test_ai_policy PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
add_test(NAME test_ai_policy COMMAND test_ai_policy)

add_executable(test_ai_runtime tests/unit/test_ai_runtime.c ${LIB_SOURCES})
target_include_directories(test_ai_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_ai_runtime PRIVATE -UNDEBUG)
target_link_libraries(test_ai_runtime PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_ai_runtime COMMAND test_ai_runtime)


add_executable(test_secret_provider tests/unit/test_secret_provider.c ${LIB_SOURCES})
target_include_directories(test_secret_provider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_secret_provider PRIVATE -UNDEBUG)
target_link_libraries(test_secret_provider PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
add_test(NAME test_secret_provider COMMAND test_secret_provider)

add_executable(test_domain_transaction tests/unit/test_domain_transaction.c ${FINANCIAL_CORE_SOURCES} src/domain/transaction/rules.c)
target_include_directories(test_domain_transaction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_transaction PRIVATE -UNDEBUG)
target_link_libraries(test_domain_transaction PRIVATE m)
add_test(NAME test_domain_transaction COMMAND test_domain_transaction)

add_executable(test_domain_asset tests/unit/test_domain_asset.c ${FINANCIAL_CORE_SOURCES} src/domain/asset/rules.c)
target_include_directories(test_domain_asset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_asset PRIVATE -UNDEBUG)
target_link_libraries(test_domain_asset PRIVATE m)
add_test(NAME test_domain_asset COMMAND test_domain_asset)

add_executable(test_domain_portfolio tests/unit/test_domain_portfolio.c ${FINANCIAL_CORE_SOURCES} src/domain/portfolio/portfolio.c src/domain/portfolio/fx_table.c src/domain/portfolio/rules.c src/domain/asset/cost_basis.c src/domain/asset/pnl.c)
target_include_directories(test_domain_portfolio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_portfolio PRIVATE -UNDEBUG)
target_link_libraries(test_domain_portfolio PRIVATE m)
add_test(NAME test_domain_portfolio COMMAND test_domain_portfolio)

add_executable(test_domain_cashflow tests/unit/test_domain_cashflow.c ${FINANCIAL_CORE_SOURCES} src/domain/cashflow/rules.c)
target_include_directories(test_domain_cashflow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_cashflow PRIVATE -UNDEBUG)
target_link_libraries(test_domain_cashflow PRIVATE m)
add_test(NAME test_domain_cashflow COMMAND test_domain_cashflow)

add_executable(test_domain_market tests/unit/test_domain_market.c ${FINANCIAL_CORE_SOURCES} src/domain/market/rules.c)
target_include_directories(test_domain_market PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_market PRIVATE -UNDEBUG)
target_link_libraries(test_domain_market PRIVATE m)
add_test(NAME test_domain_market COMMAND test_domain_market)

add_executable(test_domain_auth tests/unit/test_domain_auth.c src/domain/auth/rules.c)
target_include_directories(test_domain_auth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_auth PRIVATE -UNDEBUG)
target_link_libraries(test_domain_auth PRIVATE m)
add_test(NAME test_domain_auth COMMAND test_domain_auth)

add_executable(test_domain_ai tests/unit/test_domain_ai.c src/domain/ai/rules.c)
target_include_directories(test_domain_ai PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_ai PRIVATE -UNDEBUG)
target_link_libraries(test_domain_ai PRIVATE m)
add_test(NAME test_domain_ai COMMAND test_domain_ai)

set(CORE_FINANCIAL_SRCS
    src/core/financial/currency.c
    src/core/financial/decimal.c
    src/core/financial/money.c
    src/core/financial/price.c
    src/core/financial/quantity.c
    src/core/financial/rate.c
    src/core/financial/percentage.c
)

add_executable(test_domain_cost_basis tests/unit/test_domain_cost_basis.c src/domain/asset/cost_basis.c ${CORE_FINANCIAL_SRCS})
target_include_directories(test_domain_cost_basis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_cost_basis PRIVATE -UNDEBUG)
target_link_libraries(test_domain_cost_basis PRIVATE m)
add_test(NAME test_domain_cost_basis COMMAND test_domain_cost_basis)

add_executable(test_domain_pnl tests/unit/test_domain_pnl.c src/domain/asset/pnl.c ${CORE_FINANCIAL_SRCS})
target_include_directories(test_domain_pnl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_pnl PRIVATE -UNDEBUG)
target_link_libraries(test_domain_pnl PRIVATE m)
add_test(NAME test_domain_pnl COMMAND test_domain_pnl)

add_executable(test_domain_position tests/unit/test_domain_position.c src/domain/asset/position.c src/domain/asset/cost_basis.c src/domain/asset/pnl.c ${CORE_FINANCIAL_SRCS})
target_include_directories(test_domain_position PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_position PRIVATE -UNDEBUG)
target_link_libraries(test_domain_position PRIVATE m)
add_test(NAME test_domain_position COMMAND test_domain_position)

add_executable(test_domain_multi_currency tests/unit/test_domain_multi_currency.c src/domain/portfolio/fx_table.c ${CORE_FINANCIAL_SRCS})
target_include_directories(test_domain_multi_currency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_multi_currency PRIVATE -UNDEBUG)
target_link_libraries(test_domain_multi_currency PRIVATE m)
add_test(NAME test_domain_multi_currency COMMAND test_domain_multi_currency)

add_executable(test_database_repository tests/unit/test_database_repository.c ${LIB_SOURCES})
target_include_directories(test_database_repository PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_database_repository PRIVATE -UNDEBUG)
target_link_libraries(test_database_repository PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_database_repository COMMAND test_database_repository)

add_executable(test_migration_engine tests/unit/test_migration_engine.c ${LIB_SOURCES})
target_include_directories(test_migration_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_migration_engine PRIVATE -UNDEBUG)
target_link_libraries(test_migration_engine PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_migration_engine COMMAND test_migration_engine)












