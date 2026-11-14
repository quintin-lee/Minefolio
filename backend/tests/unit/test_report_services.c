#include "services/report_asset_service.h"
#include "services/report_expense_service.h"
#include "application/category/usecases.h"
#include "application/report/commands.h"
#include "domain/auth/repository.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include <assert.h>
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

    // 7. report_position_overview
    report_position_overview(pool, &q, &res);
    assert(res.code == 0 && res.data_payload != NULL);
    csilk_json_free(res.data_payload);
    printf("  [OK] report_position_overview\n");

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

    printf("=== Report Services Unit Tests Passed ===\n");
    return 0;
}
