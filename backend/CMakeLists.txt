cmake_minimum_required(VERSION 3.16)
project(minefolio VERSION 1.4.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries by default" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries" FORCE)
set(CSILK_BUILD_SHARED OFF CACHE BOOL "Build shared library (in addition to static)" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't update already-populated dependencies" FORCE)

FetchContent_Declare(
    csilk
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../deps/csilk"
    GIT_REPOSITORY https://github.com/quintin-lee/csilk.git
    GIT_TAG        v1.2.0
)

FetchContent_MakeAvailable(csilk)

# Opt-in sanitizer build for dev (default flags untouched):
#   cmake -S backend -B backend/build-asan -DMINEFOLIO_SANITIZE=ON
option(MINEFOLIO_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
set(MINEFOLIO_ASAN_RSS_LIMIT_MB "2048" CACHE STRING "Hard RSS limit (MB) for ASan test binaries")
if(MINEFOLIO_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CTestCustom.cmake.in"
        "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
        @ONLY
    )
else()
    file(REMOVE "${CMAKE_BINARY_DIR}/CTestCustom.cmake")
endif()

# Find libpq for PostgreSQL support
find_package(PkgConfig QUIET)
pkg_check_modules(PQ libpq)
if(PQ_FOUND)
    add_compile_definitions(HAVE_LIBPQ)
endif()

# Minefolio sources (recursive glob for all subdirectories)
file(GLOB_RECURSE SOURCES "src/*.c")
set(LIB_SOURCES ${SOURCES})
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main.c$")

find_package(CURL REQUIRED)

# decimal.c 的 __int128 定点数实现为故意设计，按文件豁免 -Wpedantic（头文件内另有 pragma 意图声明）
set_source_files_properties(src/core/financial/decimal.c PROPERTIES COMPILE_OPTIONS -Wno-pedantic)

add_library(minefolio_core STATIC ${LIB_SOURCES})
target_include_directories(minefolio_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(PQ_INCLUDE_DIRS)
    target_include_directories(minefolio_core PUBLIC ${PQ_INCLUDE_DIRS})
endif()
target_compile_definitions(minefolio_core PUBLIC
    _GNU_SOURCE
    MINEFOLIO_VERSION="${PROJECT_VERSION}"
)
target_compile_options(minefolio_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_link_libraries(minefolio_core PUBLIC csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)

add_executable(minefolio src/main.c)
target_compile_options(minefolio PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_link_libraries(minefolio PRIVATE minefolio_core)

# ---------------------------------------------------------------------------
# reset-password: 独立运维 CLI（重置用户密码/关闭 2FA）
#
# 设计：本工具**不链接 minefolio_core**——它只需 csilk 的 db + bcrypt + json
# 与数据库驱动（sqlite3 / libpq），与运行时容器已安装的共享库保持一致，
# 避免拉入整个应用层。哈希算法与 cost 与 application/auth/usecases.c 严格
# 一致，保证写回的 hash 可被 Web 登录路径 csilk_bcrypt_verify 直接验证。
#
# 链接项：csilk（驱动 + bcrypt + json）+ OpenSSL(crypto) + libpq（仅当可用）
# + pthread。工具二进制随 build 目录产出，Dockerfile 将其拷入 /usr/local/bin，
# 使 `docker exec minefolio /usr/local/bin/reset-password ...` 可直接用于后台维护。
# ---------------------------------------------------------------------------
add_executable(reset-password tools/reset-password.c)
target_compile_options(reset-password PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
target_link_libraries(reset-password PRIVATE csilk crypto pthread ${PQ_LIBRARIES})

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
        add_dependencies(minefolio_core format_sources)
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
)

function(add_minefolio_unit_test test_target test_source)
    add_executable(${test_target} ${test_source})
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
    target_link_libraries(${test_target} PRIVATE minefolio_core ${ARGN})
    add_test(NAME ${test_target} COMMAND ${test_target})
endfunction()

function(add_financial_unit_test test_target test_source)
    add_minefolio_unit_test(${test_target} ${test_source})
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
add_financial_unit_test(test_ledger_rebuild_checkpoint tests/unit/test_ledger_rebuild_checkpoint.c)
add_financial_unit_test(test_transaction_lots tests/unit/test_transaction_lots.c)
add_financial_unit_test(test_cost_basis_fifo tests/unit/test_cost_basis_fifo.c)
add_financial_unit_test(test_ai_token_compact tests/unit/test_ai_token_compact.c)
add_financial_unit_test(test_ai_batch_confirmation tests/unit/test_ai_batch_confirmation.c)
add_financial_unit_test(test_ai_action_confirm tests/unit/test_ai_action_confirm.c)

# AI Tool System (propose_batch_actions, propose_daily_expense, propose_transfer, web_search, etc.)
add_minefolio_unit_test(test_ai_tools tests/unit/test_ai_tools.c pthread)
add_minefolio_unit_test(test_ai_policy tests/unit/test_ai_policy.c)
add_minefolio_unit_test(test_mcp_pool tests/unit/test_mcp_pool.c)
add_minefolio_unit_test(test_policy_mcp_risk tests/unit/test_policy_mcp_risk.c)
add_minefolio_unit_test(test_ai_runtime tests/unit/test_ai_runtime.c)
add_minefolio_unit_test(test_ai_summary tests/unit/test_ai_summary.c)
add_minefolio_unit_test(test_ai_mask tests/unit/test_ai_mask.c m)

# 该测试经 ai_config_provider_init(NULL) 回退读取相对路径 config/ai.json。
# config/ai.json 被 .gitignore 排除，CI 干净克隆里不存在，copy_assets 也无从复制；
# 因此测试改为在 main() 里自建临时 config 并用 AI_CONFIG 绝对路径覆盖（见
# tests/unit/test_ai_config_provider.c），不依赖构建目录里的 config/。
#
# 如需恢复对构建目录 config/ 的依赖，应同时把 config/ai.json 纳入版本控制。
add_minefolio_unit_test(test_ai_config_provider tests/unit/test_ai_config_provider.c)
add_minefolio_unit_test(test_secret_provider tests/unit/test_secret_provider.c)

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

add_executable(test_domain_mcp tests/unit/test_domain_mcp.c src/domain/mcp/rules.c)
target_include_directories(test_domain_mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_mcp PRIVATE -UNDEBUG)
target_link_libraries(test_domain_mcp PRIVATE m)
add_test(NAME test_domain_mcp COMMAND test_domain_mcp)

add_executable(test_domain_skill tests/unit/test_domain_skill.c src/domain/skill/rules.c)
target_include_directories(test_domain_skill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_skill PRIVATE -UNDEBUG)
target_link_libraries(test_domain_skill PRIVATE m)
add_test(NAME test_domain_skill COMMAND test_domain_skill)

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

add_executable(test_domain_performance tests/unit/test_domain_performance.c src/domain/asset/performance.c ${CORE_FINANCIAL_SRCS})
target_include_directories(test_domain_performance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_performance PRIVATE -UNDEBUG)
target_link_libraries(test_domain_performance PRIVATE m)
add_test(NAME test_domain_performance COMMAND test_domain_performance)

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

add_minefolio_unit_test(test_database_repository tests/unit/test_database_repository.c)

add_minefolio_unit_test(test_skill_repository tests/unit/test_skill_repository.c)
add_dependencies(test_skill_repository copy_assets)

add_minefolio_unit_test(test_migration_engine tests/unit/test_migration_engine.c)
add_minefolio_unit_test(test_domain_repositories tests/unit/test_domain_repositories.c)
add_minefolio_unit_test(test_market_indices tests/unit/test_market_indices.c)
add_minefolio_unit_test(test_fx_semantics tests/unit/test_fx_semantics.c)
add_minefolio_unit_test(test_portfolio_pnl_alignment tests/unit/test_portfolio_pnl_alignment.c)
add_minefolio_unit_test(test_mcp_client_io tests/unit/test_mcp_client_io.c)
add_minefolio_unit_test(test_mcp_stderr_capture tests/unit/test_mcp_stderr_capture.c)
add_minefolio_unit_test(test_mcp_child_sanitation tests/unit/test_mcp_child_sanitation.c)
add_minefolio_unit_test(test_ai_parallel_tools tests/unit/test_ai_parallel_tools.c)
add_minefolio_unit_test(test_workflow_step_error tests/unit/test_workflow_step_error.c)
add_minefolio_unit_test(test_workflow_branching tests/unit/test_workflow_branching.c)
add_minefolio_unit_test(test_summary_worker_pool tests/unit/test_summary_worker_pool.c)
add_minefolio_unit_test(test_tx_scope_nesting tests/unit/test_tx_scope_nesting.c)
add_minefolio_unit_test(test_financial_precision_edge tests/unit/test_financial_precision_edge.c)
add_minefolio_unit_test(test_mcp_unauth_protocol tests/unit/test_mcp_unauth_protocol.c)
add_minefolio_unit_test(test_nonce_persistence tests/unit/test_nonce_persistence.c)
add_minefolio_unit_test(test_file_parser_mkstemps tests/unit/test_file_parser_mkstemps.c)
add_minefolio_unit_test(test_auth_2fa_lifecycle tests/unit/test_auth_2fa_lifecycle.c)
add_minefolio_unit_test(test_oauth_security tests/unit/test_oauth_security.c)
add_minefolio_unit_test(test_ai_prompt_injection_defense tests/unit/test_ai_prompt_injection_defense.c)

add_executable(test_domain_account tests/unit/test_domain_account.c src/domain/account/rules.c)
target_include_directories(test_domain_account PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_account PRIVATE -UNDEBUG)
target_link_libraries(test_domain_account PRIVATE m)
add_test(NAME test_domain_account COMMAND test_domain_account)

add_executable(test_domain_tag tests/unit/test_domain_tag.c src/domain/tag/rules.c)
target_include_directories(test_domain_tag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_tag PRIVATE -UNDEBUG)
target_link_libraries(test_domain_tag PRIVATE m)
add_test(NAME test_domain_tag COMMAND test_domain_tag)

add_executable(test_domain_category tests/unit/test_domain_category.c src/domain/category/rules.c)
target_include_directories(test_domain_category PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_category PRIVATE -UNDEBUG)
target_link_libraries(test_domain_category PRIVATE m)
add_test(NAME test_domain_category COMMAND test_domain_category)

add_executable(test_domain_ledger tests/unit/test_domain_ledger.c src/domain/ledger/rules.c)
target_include_directories(test_domain_ledger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_ledger PRIVATE -UNDEBUG)
target_link_libraries(test_domain_ledger PRIVATE m)
add_test(NAME test_domain_ledger COMMAND test_domain_ledger)

add_executable(test_domain_dca tests/unit/test_domain_dca.c src/domain/dca/rules.c)
target_include_directories(test_domain_dca PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_domain_dca PRIVATE -UNDEBUG)
target_link_libraries(test_domain_dca PRIVATE m)
add_test(NAME test_domain_dca COMMAND test_domain_dca)

add_minefolio_unit_test(test_account_usecases tests/unit/test_account_usecases.c)
add_dependencies(test_account_usecases copy_assets)

add_minefolio_unit_test(test_account_rbac tests/unit/test_account_rbac.c)
add_dependencies(test_account_rbac copy_assets)

add_minefolio_unit_test(test_tag_usecases tests/unit/test_tag_usecases.c)
add_dependencies(test_tag_usecases copy_assets)

add_minefolio_unit_test(test_category_usecases tests/unit/test_category_usecases.c)
add_dependencies(test_category_usecases copy_assets)

add_minefolio_unit_test(test_report_services tests/unit/test_report_services.c)
add_dependencies(test_report_services copy_assets)

add_minefolio_unit_test(test_ai_news_privacy tests/unit/test_ai_news_privacy.c)
add_dependencies(test_ai_news_privacy copy_assets)

add_minefolio_unit_test(test_scheduler_migration tests/unit/test_scheduler_migration.c)
add_dependencies(test_scheduler_migration copy_assets)

add_minefolio_unit_test(test_scheduler_repo tests/unit/test_scheduler_repo.c)
add_dependencies(test_scheduler_repo copy_assets)

add_minefolio_unit_test(test_cron_parser tests/unit/test_cron_parser.c)
add_dependencies(test_cron_parser copy_assets)

add_minefolio_unit_test(test_wechat_push_service tests/unit/test_wechat_push_service.c pthread)
add_dependencies(test_wechat_push_service copy_assets)

add_minefolio_unit_test(test_scheduler_engine tests/unit/test_scheduler_engine.c pthread)
add_dependencies(test_scheduler_engine copy_assets)

add_minefolio_unit_test(test_scheduler_idle_sleep tests/unit/test_scheduler_idle_sleep.c pthread)
add_dependencies(test_scheduler_idle_sleep copy_assets)

add_minefolio_unit_test(test_wal_checkpoint tests/unit/test_wal_checkpoint.c pthread)
add_dependencies(test_wal_checkpoint copy_assets)

add_minefolio_unit_test(test_db_backup tests/unit/test_db_backup.c pthread sqlite3)
add_dependencies(test_db_backup copy_assets)

add_minefolio_unit_test(test_async_task_manager tests/unit/test_async_task_manager.c pthread)
add_dependencies(test_async_task_manager copy_assets)

