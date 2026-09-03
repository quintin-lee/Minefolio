#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/validation.h"
#include "services/ai/tools/context.h"
#include "services/ai/policy/confirmation.h"
#include "repositories/asset_repo.h"
#include "common/db.h"
#include "common/config.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
init_test_db(const char* db_path)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_ai_tools_jwt_secret_32bytes_len", 1);

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0);
    assert(pool != NULL);

    FILE* f = fopen("sql/migration.sql", "r");
    if (!f) {
        f = fopen("../sql/migration.sql", "r");
    }
    if (!f) {
        f = fopen("backend/sql/migration.sql", "r");
    }
    assert(f != NULL);

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* sql = malloc((size_t)sz + 1);
    assert(sql != NULL);
    fread(sql, 1, (size_t)sz, f);
    sql[sz] = '\0';
    fclose(f);

    rc = csilk_db_exec(pool, sql);
    assert(rc == 0);
    free(sql);
}

int
main(void)
{
    printf("=== Running Integration Unit Test: AI Tool System ===\n");
    const char* db_file = "/tmp/test_ai_tools.db";
    init_test_db(db_file);
    csilk_db_pool_t* pool = db_get_pool();

    int64_t user_id = 1;
    int rc1 = csilk_db_exec(pool, "INSERT INTO users (id, username, password) VALUES (1, 'tester', 'hash123')");
    int rc2 = csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (1, 1, '储蓄账户', 'asset', 'cash')");
    int rc3 = csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (2, 1, '现金钱包', 'asset', 'cash')");
    int rc4 = csilk_db_exec(pool, "INSERT INTO categories (id, user_id, name, type, asset_type) VALUES (10, 1, '餐饮美食', 'expense', 'cash')");
    int rc5 = csilk_db_exec(pool, "INSERT INTO assets (id, user_id, category_id, name, current_value, currency) VALUES (101, 1, 1, '招商储蓄卡', 5000.0, 'CNY')");
    int rc6 = csilk_db_exec(pool, "INSERT INTO assets (id, user_id, category_id, name, current_value, currency) VALUES (102, 1, 2, '微信零钱', 500.0, 'CNY')");
    assert(rc1 == 0 && rc2 == 0 && rc3 == 0 && rc4 == 0 && rc5 == 0 && rc6 == 0);

    ai_tool_registry_init();
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, 1, "test_trace_123");

    /* 1. Invalid schema / Malformed JSON */
    printf("[Test 1] Testing Invalid Schema & Malformed JSON...\n");
    {
        char* res = ai_tool_dispatch(ctx, "get_assets", "{malformed_json");
        assert(res != NULL);
        assert(strstr(res, "invalid json") != NULL);
        free(res);
    }

    /* 2. Missing required field */
    printf("[Test 2] Testing Missing Required Field...\n");
    {
        char* res = ai_tool_dispatch(ctx, "get_asset_detail", "{}");
        assert(res != NULL);
        assert(strstr(res, "Missing required field") != NULL);
        free(res);
    }

    /* 3. Unknown tool */
    printf("[Test 3] Testing Unknown Tool...\n");
    {
        char* res = ai_tool_dispatch(ctx, "non_existent_tool", "{}");
        assert(res != NULL);
        assert(strstr(res, "unknown tool") != NULL);
        free(res);
    }

    /* 4. Unauthorized user */
    printf("[Test 4] Testing Unauthorized User...\n");
    {
        ai_tool_context_t* unauth_ctx = ai_tool_context_create(pool, -1, 0, NULL);
        char* res = ai_tool_dispatch(unauth_ctx, "get_assets", "{}");
        assert(res != NULL);
        assert(strstr(res, "unauthorized") != NULL || strstr(res, "invalid context") != NULL);
        free(res);
        ai_tool_context_free(unauth_ctx);
    }

    /* 5. Successful Query Tools */
    printf("[Test 5] Testing Query Tools Execution...\n");
    {
        char* res_time = ai_tool_dispatch(ctx, "get_current_time", "{}");
        assert(res_time != NULL);
        assert(strstr(res_time, "datetime") != NULL);
        free(res_time);

        char* res_assets = ai_tool_dispatch(ctx, "get_assets", "{\"page\":1,\"page_size\":10}");
        assert(res_assets != NULL);
        assert(strstr(res_assets, "\"assets\"") != NULL);
        assert(strstr(res_assets, "\"total\"") != NULL);
        free(res_assets);

        char* res_range = ai_tool_dispatch(ctx, "calculate_date_range", "{\"range_type\":\"this_month\"}");
        assert(res_range != NULL);
        assert(strstr(res_range, "start_date") != NULL);
        free(res_range);

        char* res_calc = ai_tool_dispatch(ctx, "calculate_compound_interest", "{\"principal\":10000,\"annual_rate_pct\":8.0,\"monthly_contribution\":500,\"years\":5}");
        assert(res_calc != NULL);
        assert(strstr(res_calc, "final_balance") != NULL);
        free(res_calc);
    }

    /* 6. Propose & Confirmation Token Lifecycle */
    printf("[Test 6] Testing Mutation Policy & Confirmation Token Lifecycle...\n");
    {
        /* Propose Daily Expense */
        char* prop_res = ai_tool_dispatch(ctx, "propose_daily_expense", "{\"amount\":150.0,\"category_name\":\"餐饮美食\",\"asset_name\":\"招商储蓄卡\",\"note\":\"工作日午餐\"}");
        assert(prop_res != NULL);
        assert(strstr(prop_res, "propose_success") != NULL);
        assert(strstr(prop_res, "draft_token") != NULL);

        csilk_json_t* prop_obj = csilk_json_parse(prop_res);
        assert(prop_obj != NULL);
        const char* draft_token = csilk_json_get_string(prop_obj, "draft_token");
        const char* date_str = csilk_json_get_string(prop_obj, "date");
        assert(draft_token != NULL && draft_token[0] != '\0');

        /* Test 6a: Modified Proposal (Tampering amount 150.0 -> 300.0) -> Must fail */
        printf("  [6a] Testing Tampered Proposal (modified amount)...\n");
        {
            csilk_json_t* tampered = csilk_json_object();
            csilk_json_add_number(tampered, "amount", 300.0);
            csilk_json_add_string(tampered, "type", "expense");
            csilk_json_add_string(tampered, "date", date_str ? date_str : "2026-09-02");
            csilk_json_add_number(tampered, "category_id", 10.0);
            csilk_json_add_number(tampered, "asset_id", 101.0);
            csilk_json_add_string(tampered, "draft_token", draft_token);
            size_t slen = 0;
            char* tamp_str = csilk_json_serialize(tampered, &slen);
            csilk_json_free(tampered);

            char* exec_fail = ai_tool_dispatch(ctx, "confirm_proposed_expense", tamp_str);
            free(tamp_str);
            assert(exec_fail != NULL);
            assert(strstr(exec_fail, "invalid or expired confirmation draft token") != NULL);
            free(exec_fail);
        }

        /* Test 6b: Replay / Impersonation Attack (User 2 using User 1 token) -> Must fail */
        printf("  [6b] Testing Replay / Impersonation Attack (wrong user_id)...\n");
        {
            ai_tool_context_t* user2_ctx = ai_tool_context_create(pool, 2, 1, "hacker_trace");
            csilk_json_t* replay = csilk_json_object();
            csilk_json_add_number(replay, "amount", 150.0);
            csilk_json_add_string(replay, "type", "expense");
            csilk_json_add_string(replay, "date", date_str ? date_str : "2026-09-02");
            csilk_json_add_number(replay, "category_id", 10.0);
            csilk_json_add_number(replay, "asset_id", 101.0);
            csilk_json_add_string(replay, "draft_token", draft_token);
            size_t slen = 0;
            char* replay_str = csilk_json_serialize(replay, &slen);
            csilk_json_free(replay);

            char* exec_replay = ai_tool_dispatch(user2_ctx, "confirm_proposed_expense", replay_str);
            free(replay_str);
            assert(exec_replay != NULL);
            assert(strstr(exec_replay, "invalid or expired confirmation draft token") != NULL);
            free(exec_replay);
            ai_tool_context_free(user2_ctx);
        }

        /* Test 6c: Valid Confirmation -> Must Succeed */
        printf("  [6c] Testing Legitimate Confirmation & Balance Mutation...\n");
        {
            csilk_json_t* confirm = csilk_json_object();
            csilk_json_add_number(confirm, "amount", 150.0);
            csilk_json_add_string(confirm, "type", "expense");
            csilk_json_add_string(confirm, "date", date_str ? date_str : "2026-09-02");
            csilk_json_add_number(confirm, "category_id", 10.0);
            csilk_json_add_number(confirm, "asset_id", 101.0);
            csilk_json_add_string(confirm, "note", "工作日午餐");
            csilk_json_add_string(confirm, "draft_token", draft_token);
            size_t slen = 0;
            char* conf_str = csilk_json_serialize(confirm, &slen);
            csilk_json_free(confirm);

            char* exec_ok = ai_tool_dispatch(ctx, "confirm_proposed_expense", conf_str);
            free(conf_str);
            assert(exec_ok != NULL);
            assert(strstr(exec_ok, "success") != NULL);
            free(exec_ok);

            /* Verify balance debited: 5000 - 150 = 4850 */
            csilk_json_t* asset = asset_get(pool, user_id, 101);
            assert(asset != NULL && csilk_json_array_size(asset) > 0);
            double bal = db_get_num(csilk_json_array_get(asset, 0), "current_value");
            assert(bal == 4850.0);
            csilk_json_free(asset);
        }

        csilk_json_free(prop_obj);
        free(prop_res);
    }

    /* 7. Propose & Confirm Transfer */
    printf("[Test 7] Testing Transfer Mutation & Balance Linking...\n");
    {
        char* prop_tf = ai_tool_dispatch(ctx, "propose_transfer", "{\"amount\":300.0,\"from_asset_name\":\"招商\",\"to_asset_name\":\"微信\"}");
        assert(prop_tf != NULL);
        assert(strstr(prop_tf, "draft_token") != NULL);

        csilk_json_t* tf_obj = csilk_json_parse(prop_tf);
        assert(tf_obj != NULL);
        const char* tf_token = csilk_json_get_string(tf_obj, "draft_token");
        const char* tf_date = csilk_json_get_string(tf_obj, "date");

        csilk_json_t* conf_tf = csilk_json_object();
        csilk_json_add_number(conf_tf, "amount", 300.0);
        csilk_json_add_number(conf_tf, "from_asset_id", 101.0);
        csilk_json_add_number(conf_tf, "to_asset_id", 102.0);
        csilk_json_add_string(conf_tf, "date", tf_date ? tf_date : "2026-09-02");
        csilk_json_add_string(conf_tf, "draft_token", tf_token);
        size_t slen = 0;
        char* conf_tf_str = csilk_json_serialize(conf_tf, &slen);
        csilk_json_free(conf_tf);

        char* tf_ok = ai_tool_dispatch(ctx, "confirm_proposed_transfer", conf_tf_str);
        free(conf_tf_str);
        assert(tf_ok != NULL);
        assert(strstr(tf_ok, "success") != NULL);
        free(tf_ok);

        /* Verify balances: 101 (4850 - 300 = 4550), 102 (500 + 300 = 800) */
        csilk_json_t* a101 = asset_get(pool, user_id, 101);
        csilk_json_t* a102 = asset_get(pool, user_id, 102);
        assert(a101 != NULL && csilk_json_array_size(a101) > 0);
        assert(a102 != NULL && csilk_json_array_size(a102) > 0);
        assert(db_get_num(csilk_json_array_get(a101, 0), "current_value") == 4550.0);
        assert(db_get_num(csilk_json_array_get(a102, 0), "current_value") == 800.0);
        csilk_json_free(a101);
        csilk_json_free(a102);

        csilk_json_free(tf_obj);
        free(prop_tf);
    }

    ai_tool_context_free(ctx);
    unlink(db_file);

    printf("🎉 ALL AI TOOL FRAMEWORK TESTS PASSED!\n");
    return 0;
}
