#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "interfaces/http/controllers/ai_controller.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/context.h"
#include "services/ai/policy/confirmation.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "csilk/csilk.h"

static void
init_test_db(const char* db_path, csilk_db_pool_t** out_pool)
{
    unlink(db_path);
    setenv("MINEFOLIO_DB_DRIVER", "sqlite", 1);
    setenv("MINEFOLIO_DB_DSN", db_path, 1);
    setenv("MINEFOLIO_JWT_SECRET", "test_action_confirm_jwt_secret_32bytes", 1);

    csilk_db_pool_t* pool = NULL;
    int rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

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
    size_t n = fread(sql, 1, (size_t)sz, f);
    sql[n] = '\0';
    fclose(f);

    rc = csilk_db_exec(pool, sql);
    assert(rc == 0);
    free(sql);

    *out_pool = pool;
}

static void
seed_user_and_assets(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t*         out_a1,
                     int64_t*         out_a2,
                     int64_t*         out_cat)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO users (id, username, password) VALUES (?, 'action_test_user', 'hash')",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (1, ?, '默认账本', 1)",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO accounts (id, user_id, ledger_id, name, institution) VALUES (1, ?, 1, '默认账户', 'cash_wallet')",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO categories (id, user_id, name, type, icon) VALUES (10, ?, '餐饮美食', 'expense', '')",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, ledger_id) "
        "VALUES (101, ?, 10, 1, '现金钱包', 1000.0, 'CNY', 1)",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, ledger_id) "
        "VALUES (102, ?, 10, 1, '招商储蓄卡', 2000.0, 'CNY', 1)",
        (const char*[]){uid_str, NULL}));

    if (out_a1) *out_a1 = 101;
    if (out_a2) *out_a2 = 102;
    if (out_cat) *out_cat = 10;
}

int
main(int argc, char** argv)
{
    if (argc >= 5 && strcmp(argv[1], "--sign") == 0) {
        int64_t       user_id = atoll(argv[2]);
        const char*   tool_name = argv[3];
        const char*   json_str = argv[4];
        csilk_json_t* args = csilk_json_parse(json_str);
        if (!args) {
            fprintf(stderr, "Invalid args JSON\n");
            return 1;
        }
        char* token = ai_confirmation_create_bound_token(user_id, 0, tool_name, args, AI_RISK_MEDIUM, 300);
        if (!token) {
            fprintf(stderr, "Failed to create bound token\n");
            csilk_json_free(args);
            return 1;
        }
        printf("%s\n", token);
        free(token);
        csilk_json_free(args);
        return 0;
    }

    const char* test_db = "/tmp/test_ai_action_confirm.db";
    csilk_db_pool_t* pool = NULL;
    init_test_db(test_db, &pool);

    int64_t user_id = 901;
    int64_t a1 = 0, a2 = 0, cat = 0;
    seed_user_and_assets(pool, user_id, &a1, &a2, &cat);

    ai_tool_registry_init();
    ai_tool_context_t* ctx = ai_tool_context_create(pool, user_id, 1, "trace_test_action");
    assert(ctx != NULL);

    printf("============================================================\n");
    printf("   TEST SUITE: AI ACTION CONFIRM & PERSISTENCE               \n");
    printf("============================================================\n");

    /* ----------------------------------------------------------------- */
    /* Test 1: Body Parameter Copying into args Object                  */
    /* ----------------------------------------------------------------- */
    printf("[Test 1] Argument extraction using csilk_json_add_object...\n");
    {
        csilk_json_t* body = csilk_json_object();
        csilk_json_add_string(body, "action_type", "daily_expense");
        csilk_json_add_string(body, "draft_token", "mf_cf_test_token_12345");
        csilk_json_add_number(body, "amount", 68.5);
        csilk_json_add_string(body, "type", "expense");
        csilk_json_add_string(body, "date", "2026-11-16");
        csilk_json_add_number(body, "category_id", (double)cat);
        csilk_json_add_number(body, "asset_id", (double)a1);
        csilk_json_add_string(body, "note", "Dinner at cafe");

        /* Replicate api_ai_action_confirm_handler parameter copying */
        const char* draft_token = csilk_json_get_string(body, "draft_token");
        csilk_json_t* args = csilk_json_object();
        size_t bsize = csilk_json_object_size(body);
        for (size_t i = 0; i < bsize; i++) {
            const char* key = csilk_json_object_key(body, i);
            const csilk_json_t* val = csilk_json_object_val(body, i);
            if (!key || strcmp(key, "action_type") == 0 || strcmp(key, "draft_token") == 0) {
                continue;
            }
            csilk_json_add_object(args, key, csilk_json_copy((csilk_json_t*)val));
        }
        csilk_json_add_string(args, "draft_token", draft_token);

        /* Verify all properties were properly preserved in the args object */
        assert(csilk_json_get_number(args, "amount") == 68.5);
        assert(strcmp(csilk_json_get_string(args, "type"), "expense") == 0);
        assert(strcmp(csilk_json_get_string(args, "date"), "2026-11-16") == 0);
        assert(csilk_json_get_number(args, "category_id") == (double)cat);
        assert(csilk_json_get_number(args, "asset_id") == (double)a1);
        assert(strcmp(csilk_json_get_string(args, "note"), "Dinner at cafe") == 0);
        assert(strcmp(csilk_json_get_string(args, "draft_token"), "mf_cf_test_token_12345") == 0);

        printf("  ✅ Argument extraction into args JSON object verified successfully\n");
        csilk_json_free(body);
        csilk_json_free(args);
    }

    /* ----------------------------------------------------------------- */
    /* Test 2: End-to-end Propose -> Confirm Execution for Daily Expense */
    /* ----------------------------------------------------------------- */
    printf("[Test 2] Propose daily expense -> Confirm execution...\n");
    char* saved_draft_token = NULL;
    char* saved_date = NULL;
    {
        /* 1. Call propose_daily_expense */
        char* prop_res = ai_tool_dispatch(
            ctx,
            "propose_daily_expense",
            "{\"amount\":85.0,\"category_name\":\"餐饮美食\",\"asset_name\":\"现金钱包\",\"note\":\"下午茶\"}");
        assert(prop_res != NULL);
        assert(strstr(prop_res, "propose_success") != NULL);
        assert(strstr(prop_res, "draft_token") != NULL);

        csilk_json_t* prop_obj = csilk_json_parse(prop_res);
        assert(prop_obj != NULL);
        const char* dt = csilk_json_get_string(prop_obj, "draft_token");
        assert(dt != NULL && dt[0] != '\0');
        saved_draft_token = strdup(dt);
        const char* prop_date = csilk_json_get_string(prop_obj, "date");
        if (prop_date) {
            saved_date = strdup(prop_date);
        }

        /* 2. Simulate client confirm body */
        csilk_json_t* client_body = csilk_json_object();
        csilk_json_add_string(client_body, "action_type", "daily_expense");
        csilk_json_add_string(client_body, "draft_token", saved_draft_token);
        csilk_json_add_number(client_body, "amount", 85.0);
        csilk_json_add_string(client_body, "type", "expense");
        csilk_json_add_string(client_body, "date", prop_date ? prop_date : "2026-11-16");
        csilk_json_add_number(client_body, "category_id", (double)cat);
        csilk_json_add_number(client_body, "asset_id", (double)a1);
        csilk_json_add_string(client_body, "note", "下午茶");

        /* 3. Build args object */
        csilk_json_t* args = csilk_json_object();
        size_t bsize = csilk_json_object_size(client_body);
        for (size_t i = 0; i < bsize; i++) {
            const char* key = csilk_json_object_key(client_body, i);
            const csilk_json_t* val = csilk_json_object_val(client_body, i);
            if (!key || strcmp(key, "action_type") == 0 || strcmp(key, "draft_token") == 0) {
                continue;
            }
            csilk_json_add_object(args, key, csilk_json_copy((csilk_json_t*)val));
        }
        csilk_json_add_string(args, "draft_token", saved_draft_token);

        /* 4. Dispatch confirm tool */
        char* confirm_res = ai_tool_dispatch_parsed(ctx, "confirm_proposed_expense", args);
        assert(confirm_res != NULL);
        assert(strstr(confirm_res, "success") != NULL);
        printf("  ✅ Tool execution response: %s\n", confirm_res);

        /* 5. Verify balance changed from 1000.0 to 915.0 */
        csilk_json_t* asset = asset_get(pool, user_id, 0, a1);
        assert(asset != NULL && csilk_json_array_size(asset) > 0);
        double bal = db_get_num(csilk_json_array_get(asset, 0), "current_value");
        assert(bal == 915.0);
        csilk_json_free(asset);
        printf("  ✅ Asset balance verified: 1000.0 -> %.2f\n", bal);

        /* 6. Verify daily_expenses table contains row */
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        csilk_json_t* exp_rows = csilk_db_query_param_json(
            pool,
            "SELECT id, amount, note FROM daily_expenses WHERE user_id = ? ORDER BY id DESC LIMIT 1",
            (const char*[]){uid_str, NULL});
        assert(exp_rows != NULL && csilk_json_array_size(exp_rows) > 0);
        const csilk_json_t* r0 = csilk_json_array_get(exp_rows, 0);
        assert(db_get_num(r0, "amount") == 85.0);
        assert(strcmp(csilk_json_get_string(r0, "note"), "下午茶") == 0);
        csilk_json_free(exp_rows);
        printf("  ✅ Database row in daily_expenses verified\n");

        free(confirm_res);
        csilk_json_free(args);
        csilk_json_free(client_body);
        csilk_json_free(prop_obj);
        free(prop_res);
    }

    /* ----------------------------------------------------------------- */
    /* Test 3: Anti-Replay Defense on Single Action Confirm             */
    /* ----------------------------------------------------------------- */
    printf("[Test 3] Anti-replay defense on re-executing same draft_token...\n");
    {
        csilk_json_t* replay_args = csilk_json_object();
        csilk_json_add_number(replay_args, "amount", 85.0);
        csilk_json_add_string(replay_args, "type", "expense");
        csilk_json_add_string(replay_args, "date", saved_date ? saved_date : "2026-11-17");
        csilk_json_add_number(replay_args, "category_id", (double)cat);
        csilk_json_add_number(replay_args, "asset_id", (double)a1);
        csilk_json_add_string(replay_args, "note", "下午茶");
        csilk_json_add_string(replay_args, "draft_token", saved_draft_token);

        char* replay_res = ai_tool_dispatch_parsed(ctx, "confirm_proposed_expense", replay_args);
        assert(replay_res != NULL);
        assert(strstr(replay_res, "error") != NULL);
        assert(strstr(replay_res, "已被使用") != NULL || strstr(replay_res, "replay") != NULL || strstr(replay_res, "consumed") != NULL);
        printf("  ✅ Replay blocked with expected error: %s\n", replay_res);

        free(replay_res);
        if (saved_date) {
            free(saved_date);
        }
        csilk_json_free(replay_args);
    }

    /* ----------------------------------------------------------------- */
    /* Test 4: ai_message_mark_action_executed Message Persistence      */
    /* ----------------------------------------------------------------- */
    printf("[Test 4] Verifying ai_message_mark_action_executed status rewrite...\n");
    {
        /* Create session */
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ai_sessions (id, user_id, title) VALUES (1, ?, '测试会话')",
            (const char*[]){uid_str, NULL}));

        /* Create message with action card containing saved_draft_token and pending status */
        char msg_content[2048];
        snprintf(msg_content, sizeof(msg_content),
                 "好的，已为您准备记账信息：\n"
                 "```json:minefolio-action\n"
                 "{\n"
                 "  \"action_type\": \"daily_expense\",\n"
                 "  \"amount\": 85.0,\n"
                 "  \"draft_token\": \"%s\",\n"
                 "  \"status\": \"pending\"\n"
                 "}\n"
                 "```\n"
                 "请在上方卡片点击确认记账入库。",
                 saved_draft_token);

        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ai_messages (id, session_id, role, content) VALUES (1, 1, 'assistant', ?)",
            (const char*[]){msg_content, NULL}));

        /* Execute status update */
        ai_message_mark_action_executed(pool, user_id, saved_draft_token);

        /* Query message content and assert status was updated to executed */
        csilk_json_t* check = csilk_db_query_param_json(
            pool,
            "SELECT content FROM ai_messages WHERE id = ?",
            (const char*[]){"1", NULL});
        assert(check != NULL && csilk_json_array_size(check) > 0);
        const char* updated_content = csilk_json_get_string(csilk_json_array_get(check, 0), "content");
        assert(updated_content != NULL);

        assert(strstr(updated_content, "\"status\": \"executed\"") != NULL || strstr(updated_content, "\"status\":\"executed\"") != NULL);
        assert(strstr(updated_content, "pending") == NULL);
        assert(strstr(updated_content, "好的，已为您准备记账信息：") != NULL);
        assert(strstr(updated_content, "请在上方卡片点击确认记账入库。") != NULL);
        printf("  ✅ Message content status successfully updated to 'executed':\n%s\n", updated_content);
        char* saved_content = strdup(updated_content);
        csilk_json_free(check);

        /* Idempotency check: Calling it again does not corrupt content */
        ai_message_mark_action_executed(pool, user_id, saved_draft_token);
        csilk_json_t* check2 = csilk_db_query_param_json(
            pool,
            "SELECT content FROM ai_messages WHERE id = ?",
            (const char*[]){"1", NULL});
        assert(check2 != NULL && csilk_json_array_size(check2) > 0);
        const char* updated_content2 = csilk_json_get_string(csilk_json_array_get(check2, 0), "content");
        assert(strcmp(saved_content, updated_content2) == 0);
        free(saved_content);
        csilk_json_free(check2);
        printf("  ✅ Idempotent execution verified\n");
    }

    /* ----------------------------------------------------------------- */
    /* Test 5: Propose & Confirm Transfer with Message Persistence      */
    /* ----------------------------------------------------------------- */
    printf("[Test 5] Propose & Confirm Transfer with Message Persistence...\n");
    {
        char* prop_tf = ai_tool_dispatch(
            ctx,
            "propose_transfer",
            "{\"amount\":120.0,\"from_asset_name\":\"招商储蓄卡\",\"to_asset_name\":\"现金钱包\",\"note\":\"提现\"}");
        assert(prop_tf != NULL);
        assert(strstr(prop_tf, "draft_token") != NULL);

        csilk_json_t* tf_obj = csilk_json_parse(prop_tf);
        assert(tf_obj != NULL);
        const char* tf_token = csilk_json_get_string(tf_obj, "draft_token");
        const char* tf_date = csilk_json_get_string(tf_obj, "date");
        assert(tf_token != NULL && tf_token[0] != '\0');

        /* Insert message with transfer action card */
        char tf_msg_content[2048];
        snprintf(tf_msg_content, sizeof(tf_msg_content),
                 "```json:minefolio-action\n"
                 "{\n"
                 "  \"action_type\": \"transfer\",\n"
                 "  \"amount\": 120.0,\n"
                 "  \"draft_token\": \"%s\",\n"
                 "  \"status\": \"pending\"\n"
                 "}\n"
                 "```\n",
                 tf_token);
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ai_messages (id, session_id, role, content) VALUES (2, 1, 'assistant', ?)",
            (const char*[]){tf_msg_content, NULL}));

        /* Build args */
        csilk_json_t* tf_args = csilk_json_object();
        csilk_json_add_number(tf_args, "amount", 120.0);
        csilk_json_add_number(tf_args, "from_asset_id", (double)a2);
        csilk_json_add_number(tf_args, "to_asset_id", (double)a1);
        csilk_json_add_string(tf_args, "date", tf_date ? tf_date : "2026-11-16");
        csilk_json_add_string(tf_args, "note", "提现");
        csilk_json_add_string(tf_args, "draft_token", tf_token);

        char* tf_exec = ai_tool_dispatch_parsed(ctx, "confirm_proposed_transfer", tf_args);
        assert(tf_exec != NULL);
        assert(strstr(tf_exec, "success") != NULL);
        printf("  ✅ Transfer confirm response: %s\n", tf_exec);

        /* Verify balances: a2 was 2000 - 120 = 1880, a1 was 915 + 120 = 1035 */
        csilk_json_t* a1_json = asset_get(pool, user_id, 0, a1);
        csilk_json_t* a2_json = asset_get(pool, user_id, 0, a2);
        assert(db_get_num(csilk_json_array_get(a1_json, 0), "current_value") == 1035.0);
        assert(db_get_num(csilk_json_array_get(a2_json, 0), "current_value") == 1880.0);
        csilk_json_free(a1_json);
        csilk_json_free(a2_json);
        printf("  ✅ Transfer asset balances updated successfully\n");

        /* Update message */
        ai_message_mark_action_executed(pool, user_id, tf_token);
        csilk_json_t* tf_check = csilk_db_query_param_json(
            pool,
            "SELECT content FROM ai_messages WHERE id = ?",
            (const char*[]){"2", NULL});
        assert(tf_check != NULL && csilk_json_array_size(tf_check) > 0);
        const char* tf_upd_content = csilk_json_get_string(csilk_json_array_get(tf_check, 0), "content");
        assert(strstr(tf_upd_content, "\"status\": \"executed\"") != NULL || strstr(tf_upd_content, "\"status\":\"executed\"") != NULL);
        assert(strstr(tf_upd_content, "pending") == NULL);
        csilk_json_free(tf_check);
        printf("  ✅ Transfer message status updated to 'executed'\n");

        free(tf_exec);
        csilk_json_free(tf_args);
        csilk_json_free(tf_obj);
        free(prop_tf);
    }

    /* ----------------------------------------------------------------- */
    /* Test 6: tool_calls column update & proposed status in content    */
    /* ----------------------------------------------------------------- */
    printf("[Test 6] Verifying tool_calls column update & proposed status in content...\n");
    {
        const char* tc_token = "mf_v2.v2|1|1|confirm_proposed_expense|dummy_hash|2|1732000000|nctest6|1732000300.dummymac";

        /* 6a: draft_token in tool_calls only (natural language content without code block) */
        char tc_json[2048];
        snprintf(tc_json, sizeof(tc_json),
                 "[{\"id\":\"call_test6\",\"name\":\"propose_daily_expense\",\"result\":\"{\\\"action_type\\\":\\\"daily_expense\\\",\\\"propose_success\\\":true,\\\"amount\\\":30.0,\\\"draft_token\\\":\\\"%s\\\"}\"}]",
                 tc_token);

        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ai_messages (id, session_id, role, content, tool_calls) VALUES (3, 1, 'assistant', '已为您生成记账卡片，请点击确认。', ?)",
            (const char*[]){tc_json, NULL}));

        ai_message_mark_action_executed(pool, user_id, tc_token);

        csilk_json_t* check3 = csilk_db_query_param_json(
            pool,
            "SELECT tool_calls FROM ai_messages WHERE id = ?",
            (const char*[]){"3", NULL});
        assert(check3 != NULL && csilk_json_array_size(check3) > 0);
        const char* upd_tc = csilk_json_get_string(csilk_json_array_get(check3, 0), "tool_calls");
        assert(upd_tc != NULL);
        assert(strstr(upd_tc, "\\\"status\\\":\\\"executed\\\"") != NULL || strstr(upd_tc, "\\\"status\\\": \\\"executed\\\"") != NULL);
        csilk_json_free(check3);
        printf("  ✅ tool_calls column successfully updated with 'executed' status\n");

        /* 6b: content with 'proposed' status */
        const char* prop_token = "mf_v2.v2|1|1|confirm_proposed_expense|dummy_hash2|2|1732000000|nctest6b|1732000300.dummymac";
        char prop_content[1024];
        snprintf(prop_content, sizeof(prop_content),
                 "```action\n{\"action_type\":\"daily_expense\",\"amount\":20,\"status\":\"proposed\",\"draft_token\":\"%s\"}\n```",
                 prop_token);

        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ai_messages (id, session_id, role, content) VALUES (4, 1, 'assistant', ?)",
            (const char*[]){prop_content, NULL}));

        ai_message_mark_action_executed(pool, user_id, prop_token);

        csilk_json_t* check4 = csilk_db_query_param_json(
            pool,
            "SELECT content FROM ai_messages WHERE id = ?",
            (const char*[]){"4", NULL});
        assert(check4 != NULL && csilk_json_array_size(check4) > 0);
        const char* upd_prop = csilk_json_get_string(csilk_json_array_get(check4, 0), "content");
        assert(upd_prop != NULL);
        assert(strstr(upd_prop, "\"status\":\"executed\"") != NULL || strstr(upd_prop, "\"status\": \"executed\"") != NULL);
        assert(strstr(upd_prop, "\"status\":\"proposed\"") == NULL);
        csilk_json_free(check4);
        printf("  ✅ Content with 'proposed' status successfully updated to 'executed'\n");
    }

    /* === 7. Regression: non-v2 format tokens are rejected without recursion ===
       verify_and_consume 曾把非 v2 令牌回退给 verify_token，后者又反向调用
       verify_and_consume 形成无限递归（工具线程栈溢出崩溃）。
       修复后任何非 v2 前缀令牌直接返回 FORMAT 错，无递归、无崩溃。 */
    {
        ai_confirmation_status_t st;
        st = ai_confirmation_verify_and_consume(user_id, 1, "legacy_action", NULL, "legacy_token");
        assert(st == AI_CONFIRM_ERR_FORMAT);
        st = ai_confirmation_verify_and_consume(user_id, 1, "legacy_action", NULL, "mf_batch_v1.forged.mac");
        assert(st == AI_CONFIRM_ERR_FORMAT);
        st = ai_confirmation_verify_and_consume(user_id, 1, "legacy_action", NULL, "");
        assert(st == AI_CONFIRM_ERR_FORMAT);
        st = ai_confirmation_verify_and_consume(user_id, 1, "legacy_action", NULL, "");
        assert(st == AI_CONFIRM_ERR_FORMAT);

        /* 兼容接口 verify_token：非 v2 前缀直接返回 0，不再进入 verify_and_consume */
        assert(ai_confirmation_verify_token(user_id, 0.0, NULL, NULL, "legacy_token") == 0);
        assert(ai_confirmation_verify_token(user_id, 0.0, NULL, NULL, "") == 0);
        assert(ai_confirmation_verify_token(user_id, 0.0, NULL, NULL, NULL) == 0);

        printf("  ✅ Non-v2 tokens rejected without recursion (legacy regression)\n");
    }

    if (saved_draft_token) {
        free(saved_draft_token);
    }
    ai_tool_context_free(ctx);
    csilk_db_pool_free(pool);
    unlink(test_db);

    printf("============================================================\n");
    printf("   ALL AI ACTION CONFIRM TESTS PASSED SUCCESSFULLY!          \n");
    printf("============================================================\n");
    return 0;
}
