#include "services/report_asset_service.h"
#include "services/report_expense_service.h"
#include "services/market/exchange_rate_service.h"
#include "application/category/usecases.h"
#include "application/report/commands.h"
#include "domain/auth/repository.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "core/ledger/ledger_engine.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_report_services.db";

static csilk_db_pool_t* setup_test_db(void) {
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_report_services_32b!");

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);

    return pool;
}

int main(void) {
    printf("=== Starting Report Services Unit Tests ===\n");
    ledger_register_balance_hooks();
    csilk_db_pool_t* pool = setup_test_db();

    int64_t user_id = 0;
    int rc = mf_auth_repo_create(pool, "report_tester", "hash_rep_123456", &user_id);
    assert(rc == 0 && user_id > 0);

    // Setup ledger
    csilk_db_exec_param(pool, "INSERT INTO ledgers (user_id, name, is_default) VALUES (?, '默认账本', 1);",
                        (const char*[]){"1", NULL});
    int64_t ledger_id = ledger_get_default(pool, user_id);
    assert(ledger_id > 0);

    // Seed categories
    category_usecase_seed_defaults(pool, user_id, ledger_id);

    // Find category IDs for income, expense, and asset
    csilk_json_t* cat_rows = csilk_db_query_param_json(
        pool,
        "SELECT id, type, asset_type FROM categories WHERE user_id=? ORDER BY id ASC LIMIT 5;",
        (const char*[]){"1", NULL});
    assert(cat_rows != NULL && csilk_json_array_size(cat_rows) > 0);
    int64_t cat_id = db_get_int(csilk_json_array_get(cat_rows, 0), "id");
    csilk_json_free(cat_rows);

    // Seed test assets: wallet (cash 10000), stock (stock 50000), liability (-2000)
    char cat_str[32], uid_str[32], lid_str[32];
    snprintf(cat_str, sizeof(cat_str), "%lld", (long long)cat_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, current_value, cost_basis, net_value) "
                        "VALUES (?, ?, ?, '现金钱包', 'CNY', 10000.0, 10000.0, 10000.0);",
                        (const char*[]){uid_str, lid_str, cat_str, NULL});

    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, current_value, cost_basis, net_value) "
                        "VALUES (?, ?, ?, '贵州茅台', 'CNY', 50000.0, 40000.0, 50000.0);",
                        (const char*[]){uid_str, lid_str, cat_str, NULL});

    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, current_value, cost_basis, net_value) "
                        "VALUES (?, ?, ?, '招行信用卡', 'CNY', 2000.0, 2000.0, 2000.0);",
                        (const char*[]){uid_str, lid_str, cat_str, NULL});

    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, current_value, cost_basis, net_value) "
                        "VALUES (?, ?, ?, 'Apple Inc', 'USD', 2000.0, 1800.0, 2000.0);",
                        (const char*[]){uid_str, lid_str, cat_str, NULL});

    // Seed daily expenses
    time_t now = time(NULL);
    struct tm cur_tm;
    localtime_r(&now, &cur_tm);
    char cur_year[16], cur_month[16], today_str[32];
    strftime(cur_year, sizeof(cur_year), "%Y", &cur_tm);
    strftime(cur_month, sizeof(cur_month), "%m", &cur_tm);
    strftime(today_str, sizeof(today_str), "%Y-%m-%d", &cur_tm);

    csilk_db_exec_param(pool,
                        "INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date) "
                        "VALUES (?, ?, ?, 1, 'income', 15000.0, 'CNY', ?);",
                        (const char*[]){uid_str, lid_str, cat_str, today_str, NULL});

    csilk_db_exec_param(pool,
                        "INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date) "
                        "VALUES (?, ?, ?, 1, 'expense', 500.0, 'CNY', ?);",
                        (const char*[]){uid_str, lid_str, cat_str, today_str, NULL});

    // 1. Test error handling on nulls
    report_result_t res;
    report_asset_trend(NULL, NULL, &res);
    assert(res.code == 1002);

    report_expense_monthly(NULL, NULL, &res);
    assert(res.code == 1002);

    report_query_t q = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .year = cur_year,
        .month = cur_month,
        .days = 30,
        .period = "30d",
        .base_currency = "CNY",
    };

    // 2. report_asset_trend
    report_asset_trend(pool, &q, &res);
    assert(res.code == 0);
    assert(res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_asset_trend (30d)\n");

    q.period = "90d";
    report_asset_trend(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);

    q.period = "365d";
    report_asset_trend(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_asset_trend (90d & 365d)\n");

    // 3. report_asset_breakdown
    report_asset_breakdown(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_asset_breakdown\n");

    // 4. report_asset_summary
    report_asset_summary(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_asset_summary\n");

    // 5. report_multi_currency_summary
    report_multi_currency_summary(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_multi_currency_summary\n");

    // 6. report_fx_pnl
    report_fx_pnl(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_fx_pnl\n");

    // 7. report_position_overview (with multi-currency daily_pnl verification)
    assert(exchange_rate_set("USD", 7.20) == 0);

    csilk_json_t* cny_row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM assets WHERE user_id=? AND name='现金钱包' LIMIT 1;",
        (const char*[]){uid_str, NULL});
    assert(cny_row && csilk_json_array_size(cny_row) == 1);
    int64_t cny_asset_id = db_get_int(csilk_json_array_get(cny_row, 0), "id");
    csilk_json_free(cny_row);

    csilk_json_t* usd_row = csilk_db_query_param_json(
        pool,
        "SELECT id FROM assets WHERE user_id=? AND name='Apple Inc' LIMIT 1;",
        (const char*[]){uid_str, NULL});
    assert(usd_row && csilk_json_array_size(usd_row) == 1);
    int64_t usd_asset_id = db_get_int(csilk_json_array_get(usd_row, 0), "id");
    csilk_json_free(usd_row);

    char cny_aid_str[32], usd_aid_str[32], log_ts[32];
    snprintf(cny_aid_str, sizeof(cny_aid_str), "%lld", (long long)cny_asset_id);
    snprintf(usd_aid_str, sizeof(usd_aid_str), "%lld", (long long)usd_asset_id);
    snprintf(log_ts, sizeof(log_ts), "%s 12:00:00", today_str);

    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO asset_balance_logs "
               "(asset_id, user_id, delta, balance_after, source_type, source_id, note, created_at) "
               "VALUES (?, ?, 100.0, 2100.0, 'test_fx', 1, 'USD delta', ?);",
               (const char*[]){usd_aid_str, uid_str, log_ts, NULL}) == 0);

    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO asset_balance_logs "
               "(asset_id, user_id, delta, balance_after, source_type, source_id, note, created_at) "
               "VALUES (?, ?, 50.0, 10050.0, 'test_fx', 2, 'CNY delta', ?);",
               (const char*[]){cny_aid_str, uid_str, log_ts, NULL}) == 0);

    report_position_overview(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    double daily_pnl = db_get_num(res.data_payload, "daily_pnl");
    assert(fabs(daily_pnl - 770.00) < 1e-4);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_position_overview (daily_pnl=%.2f)\n", daily_pnl);

    // 8. report_expense_monthly
    report_expense_monthly(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_expense_monthly\n");

    // 9. report_expense_trend
    report_expense_trend(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_expense_trend\n");

    // 10. report_expense_yearly
    report_expense_yearly(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_expense_yearly\n");

    // 11. report_expense_category
    report_expense_category(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_expense_category\n");

    // 12. report_expense_tag
    report_expense_tag(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_expense_tag\n");

    // 13. Yearly category fallback, blank/uncategorized items, and strict year/month validation
    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, '   ', 'expense');",
               (const char*[]){uid_str, lid_str, NULL}) == 0);
    csilk_json_t* blank_cat_rows = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND name='   ' LIMIT 1;",
        (const char*[]){uid_str, NULL});
    assert(blank_cat_rows && csilk_json_array_size(blank_cat_rows) == 1);
    char blank_cat_str[32];
    snprintf(blank_cat_str,
             sizeof(blank_cat_str),
             "%lld",
             (long long)db_get_int(csilk_json_array_get(blank_cat_rows, 0), "id"));
    csilk_json_free(blank_cat_rows);

    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, "
               "expense_type, amount, currency, expense_date) "
               "VALUES (?, ?, ?, 1, 'expense', 123.45, 'CNY', '2024-03-10');",
               (const char*[]){uid_str, lid_str, blank_cat_str, NULL}) == 0);

    report_query_t q_hist_yr = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .year = "2024",
        .month = NULL,
        .base_currency = "CNY",
    };
    report_expense_category(pool, &q_hist_yr, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    assert(strcmp(csilk_json_get_string(res.data_payload, "period"), "2024-") == 0);
    const csilk_json_t* cat_items = csilk_json_get(res.data_payload, "items");
    assert(cat_items && csilk_json_array_size(cat_items) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(cat_items, 0), "name"), "未分类") == 0);
    assert(fabs(db_get_num(csilk_json_array_get(cat_items, 0), "amount") - 123.45) < 1e-4);
    csilk_json_free(res.data_payload);

    report_query_t q_bad_wild = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .year = "2026%",
        .month = "01",
    };
    report_expense_monthly(pool, &q_bad_wild, &res);
    assert(res.code == 1002);
    report_expense_category(pool, &q_bad_wild, &res);
    assert(res.code == 1002);
    printf("  [OK] report_expense yearly, uncategorized, and strict validation\n");

    printf("=== Report Services Unit Tests Passed ===\n");
    return 0;
}
