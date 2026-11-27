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
#include "services/ai/policy/permission.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "application/account/usecases.h"
#include "core/financial/money.h"
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

        /* Verify V014 paired transfer_out / transfer_in transactions linked to transfers.from_tx_id / to_tx_id */
        {
            char uid_str[32];
            snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
            csilk_json_t* tf_rows = csilk_db_query_param_json(
                pool,
                "SELECT id, from_tx_id, to_tx_id FROM transfers WHERE user_id=? ORDER BY id DESC LIMIT 1",
                (const char*[]){uid_str, NULL});
            assert(tf_rows != NULL && csilk_json_array_size(tf_rows) == 1);
            const csilk_json_t* tf_r = csilk_json_array_get(tf_rows, 0);
            int64_t from_tx_id = db_get_int(tf_r, "from_tx_id");
            int64_t to_tx_id = db_get_int(tf_r, "to_tx_id");
            assert(from_tx_id > 0 && to_tx_id > 0);
            csilk_json_free(tf_rows);

            char ftx_str[32], ttx_str[32];
            snprintf(ftx_str, sizeof(ftx_str), "%lld", (long long)from_tx_id);
            snprintf(ttx_str, sizeof(ttx_str), "%lld", (long long)to_tx_id);

            csilk_json_t* out_rows = csilk_db_query_param_json(
                pool,
                "SELECT id, asset_id, linked_asset_id, transaction_type, amount, fx_rate_to_cny "
                "FROM transactions WHERE id=?",
                (const char*[]){ftx_str, NULL});
            assert(out_rows != NULL && csilk_json_array_size(out_rows) == 1);
            const csilk_json_t* out_r = csilk_json_array_get(out_rows, 0);
            assert(strcmp(csilk_json_get_string(out_r, "transaction_type"), "transfer_out") == 0);
            assert(db_get_int(out_r, "asset_id") == a2);
            assert(db_get_int(out_r, "linked_asset_id") == a1);
            assert(db_get_num(out_r, "amount") == 120.0);
            assert(db_get_num(out_r, "fx_rate_to_cny") == 1.0);
            csilk_json_free(out_rows);

            csilk_json_t* in_rows = csilk_db_query_param_json(
                pool,
                "SELECT id, asset_id, linked_asset_id, transaction_type, parent_tx_id, amount, fx_rate_to_cny "
                "FROM transactions WHERE id=?",
                (const char*[]){ttx_str, NULL});
            assert(in_rows != NULL && csilk_json_array_size(in_rows) == 1);
            const csilk_json_t* in_r = csilk_json_array_get(in_rows, 0);
            assert(strcmp(csilk_json_get_string(in_r, "transaction_type"), "transfer_in") == 0);
            assert(db_get_int(in_r, "asset_id") == a1);
            assert(db_get_int(in_r, "linked_asset_id") == a2);
            assert(db_get_int(in_r, "parent_tx_id") == from_tx_id);
            assert(db_get_num(in_r, "amount") == 120.0);
            assert(db_get_num(in_r, "fx_rate_to_cny") == 1.0);
            csilk_json_free(in_rows);
            printf("  ✅ V014 transfer pairing (transfer_out + transfer_in + from_tx_id/to_tx_id) verified\n");
        }

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

    /* ----------------------------------------------------------------- */
    /* Test 8: Backdated AI Expense Confirm Invalidates Snapshots From Date */
    /* ----------------------------------------------------------------- */
    printf("[Test 8] Backdated AI expense confirm invalidates ledger_snapshots from date...\n");
    {
        money_t snap_bal;
        money_from_double(1035.0, CURRENCY_CNY, &snap_bal);
        assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-05", NULL, snap_bal, "2026-11-05", 1, NULL) == 0);
        assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-12", NULL, snap_bal, "2026-11-12", 2, NULL) == 0);
        assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-18", NULL, snap_bal, "2026-11-18", 3, NULL) == 0);

        csilk_json_t* exp_args = csilk_json_object();
        csilk_json_add_number(exp_args, "amount", 35.0);
        csilk_json_add_string(exp_args, "type", "expense");
        csilk_json_add_string(exp_args, "date", "2026-11-11");
        csilk_json_add_number(exp_args, "category_id", (double)cat);
        csilk_json_add_number(exp_args, "asset_id", (double)a1);
        csilk_json_add_string(exp_args, "note", "Backdated snack");
        char* dt_tok = ai_confirmation_create_bound_token(
            user_id, 1, "confirm_proposed_expense", exp_args, AI_RISK_HIGH, 300);
        assert(dt_tok != NULL);
        csilk_json_add_string(exp_args, "draft_token", dt_tok);

        char* res_str = ai_tool_dispatch_parsed(ctx, "confirm_proposed_expense", exp_args);
        assert(res_str != NULL);
        assert(strstr(res_str, "success") != NULL);
        free(res_str);
        free(dt_tok);
        csilk_json_free(exp_args);

        /* Snapshot before 2026-11-11 must survive; snapshots >= 2026-11-11 must be invalidated */
        csilk_json_t* s_before = NULL;
        assert(ledger_snapshot_get_latest(pool, user_id, a1, "2026-11-10", &s_before) == 0);
        assert(s_before != NULL);
        assert(strcmp(csilk_json_get_string(s_before, "snapshot_date"), "2026-11-05") == 0);
        csilk_json_free(s_before);

        csilk_json_t* s_after = NULL;
        assert(ledger_snapshot_get_latest(pool, user_id, a1, "2026-11-20", &s_after) == 0);
        assert(s_after != NULL);
        assert(strcmp(csilk_json_get_string(s_after, "snapshot_date"), "2026-11-05") == 0);
        csilk_json_free(s_after);
        printf("  ✅ Backdated AI expense confirm invalidated snapshots >= 2026-11-11 and preserved 2026-11-05\n");
    }

    /* ----------------------------------------------------------------- */
    /* Test 9: Shared-ledger editor with viewer account perm rejected (1004) */
    /* ----------------------------------------------------------------- */
    printf("[Test 9] Shared-ledger editor with viewer permission rejected (1004) on AI confirm...\n");
    {
        int64_t owner_id = 902;
        int64_t editor_id = 903;
        int64_t sh_lid = 2;
        int64_t acct_ro = 20;
        int64_t acct_rw = 21;
        int64_t asset_ro = 201;
        int64_t asset_rw = 202;

        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO users (id, username, password) VALUES (902, 'sh_owner_ai', 'h'), (903, 'sh_editor_ai', 'h')",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (2, 902, '家庭共享账本', 0)",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (2, 902, 'owner'), (2, 903, 'editor')",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO accounts (id, user_id, ledger_id, name, institution) VALUES "
            "(20, 902, 2, '只读卡', 'bank_card'), (21, 902, 2, '日常卡', 'bank_card')",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO categories (id, user_id, ledger_id, name, type, icon) VALUES (20, 902, 2, '餐饮', 'expense', '')",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, ledger_id) VALUES "
            "(201, 902, 20, 20, '只读资产', 1000.0, 'CNY', 2), "
            "(202, 902, 20, 21, '可写资产', 1000.0, 'CNY', 2)",
            (const char*[]){NULL}));

        account_usecase_result_t ares = {0};
        assert(account_permission_usecase_set(pool, owner_id, sh_lid, acct_ro, editor_id, "viewer", 0, 0, &ares) == 0);
        assert(account_permission_usecase_set(pool, owner_id, sh_lid, acct_rw, editor_id, "operator", 0, 0, &ares) == 0);

        ai_tool_context_t* ed_ctx = ai_tool_context_create(pool, editor_id, 10, "trace_rbac");
        assert(ed_ctx != NULL);
        csilk_json_add_int(ed_ctx->metadata, "ledger_id", sh_lid);

        /* 9a. confirm_proposed_expense on viewer account rejected with 1004 */
        csilk_json_t* e_args = csilk_json_object();
        csilk_json_add_number(e_args, "amount", 50.0);
        csilk_json_add_string(e_args, "type", "expense");
        csilk_json_add_string(e_args, "date", "2026-11-20");
        csilk_json_add_number(e_args, "category_id", 20.0);
        csilk_json_add_number(e_args, "asset_id", (double)asset_ro);
        csilk_json_add_string(e_args, "note", "Unauthorized expense");
        char* e_tok = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_expense", e_args, AI_RISK_HIGH, 300);
        assert(e_tok != NULL);
        csilk_json_add_string(e_args, "draft_token", e_tok);

        char* e_res = ai_tool_dispatch_parsed(ed_ctx, "confirm_proposed_expense", e_args);
        assert(e_res != NULL);
        csilk_json_t* e_parsed = csilk_json_parse(e_res);
        assert(e_parsed != NULL);
        assert(!csilk_json_get_bool(e_parsed, "success"));
        assert(csilk_json_get_string(e_parsed, "error") != NULL);
        assert(db_get_int(e_parsed, "code") == 1004);
        csilk_json_free(e_parsed);
        free(e_res);
        free(e_tok);
        csilk_json_free(e_args);

        /* Balance must remain untouched at 1000.0 */
        csilk_json_t* aro_row = asset_get_raw(pool, owner_id, asset_ro);
        assert(db_get_num(csilk_json_array_get(aro_row, 0), "current_value") == 1000.0);
        csilk_json_free(aro_row);

        /* 9b. confirm_proposed_transfer touching viewer account (source OR target) rejected with 1004 */
        ai_permission_set_user_role(editor_id, AI_USER_ROLE_ADMIN);
        csilk_json_t* t_args = csilk_json_object();
        csilk_json_add_number(t_args, "amount", 100.0);
        csilk_json_add_number(t_args, "from_asset_id", (double)asset_ro);
        csilk_json_add_number(t_args, "to_asset_id", (double)asset_rw);
        csilk_json_add_string(t_args, "date", "2026-11-20");
        csilk_json_add_string(t_args, "note", "Unauthorized transfer out");
        char* t_tok = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_transfer", t_args, AI_RISK_HIGH, 300);
        assert(t_tok != NULL);
        csilk_json_add_string(t_args, "draft_token", t_tok);

        char* t_res = ai_tool_dispatch_parsed(ed_ctx, "confirm_proposed_transfer", t_args);
        assert(t_res != NULL);
        csilk_json_t* t_parsed = csilk_json_parse(t_res);
        assert(t_parsed != NULL);
        assert(!csilk_json_get_bool(t_parsed, "success"));
        assert(csilk_json_get_string(t_parsed, "error") != NULL);
        assert(db_get_int(t_parsed, "code") == 1004);
        csilk_json_free(t_parsed);
        free(t_res);
        free(t_tok);
        csilk_json_free(t_args);

        /* Also target viewer account */
        csilk_json_t* t_args2 = csilk_json_object();
        csilk_json_add_number(t_args2, "amount", 100.0);
        csilk_json_add_number(t_args2, "from_asset_id", (double)asset_rw);
        csilk_json_add_number(t_args2, "to_asset_id", (double)asset_ro);
        csilk_json_add_string(t_args2, "date", "2026-11-20");
        csilk_json_add_string(t_args2, "note", "Unauthorized transfer in");
        char* t_tok2 = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_transfer", t_args2, AI_RISK_HIGH, 300);
        assert(t_tok2 != NULL);
        csilk_json_add_string(t_args2, "draft_token", t_tok2);

        char* t_res2 = ai_tool_dispatch_parsed(ed_ctx, "confirm_proposed_transfer", t_args2);
        assert(t_res2 != NULL);
        csilk_json_t* t_parsed2 = csilk_json_parse(t_res2);
        assert(t_parsed2 != NULL);
        assert(!csilk_json_get_bool(t_parsed2, "success"));
        assert(db_get_int(t_parsed2, "code") == 1004);
        csilk_json_free(t_parsed2);
        free(t_res2);
        free(t_tok2);
        csilk_json_free(t_args2);

        aro_row = asset_get_raw(pool, owner_id, asset_ro);
        csilk_json_t* arw_row = asset_get_raw(pool, owner_id, asset_rw);
        assert(db_get_num(csilk_json_array_get(aro_row, 0), "current_value") == 1000.0);
        assert(db_get_num(csilk_json_array_get(arw_row, 0), "current_value") == 1000.0);
        csilk_json_free(aro_row);
        csilk_json_free(arw_row);

        ai_tool_context_free(ed_ctx);
        printf("  ✅ Shared-ledger editor with viewer permission rejected (1004) by expense & transfer confirm\n");

        /* 9c. Positive operator confirm on shared ledger even when metadata["ledger_id"] defaults to editor's personal ledger */
        int64_t personal_lid = 3;
        int64_t acct_rw2 = 22;
        int64_t asset_rw2 = 203;
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (3, 903, 'CarolPersonal', 1)",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO accounts (id, user_id, ledger_id, name, institution) VALUES "
            "(22, 902, 2, '备用可写卡', 'bank_card')",
            (const char*[]){NULL}));
        csilk_json_free(csilk_db_query_param_json(
            pool,
            "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, ledger_id) VALUES "
            "(203, 902, 20, 22, '备用可写资产', 500.0, 'CNY', 2)",
            (const char*[]){NULL}));
        assert(account_permission_usecase_set(pool, owner_id, sh_lid, acct_rw2, editor_id, "operator", 0, 0, &ares) == 0);

        ai_tool_context_t* ed_def_ctx = ai_tool_context_create(pool, editor_id, 10, "trace_op_default");
        assert(ed_def_ctx != NULL);
        /* Simulate ctx_ledger_id fallback to editor's personal default ledger (not explicit) */
        csilk_json_add_int(ed_def_ctx->metadata, "ledger_id", personal_lid);

        /* Positive expense on operator asset_rw -> succeeds, updates balance, stamps sh_lid (2, not 3) */
        csilk_json_t* ok_ea = csilk_json_object();
        csilk_json_add_number(ok_ea, "amount", 60.0);
        csilk_json_add_string(ok_ea, "type", "expense");
        csilk_json_add_string(ok_ea, "date", "2026-11-20");
        csilk_json_add_number(ok_ea, "category_id", 20.0);
        csilk_json_add_number(ok_ea, "asset_id", (double)asset_rw);
        csilk_json_add_string(ok_ea, "note", "Shared op expense");
        char* ok_et = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_expense", ok_ea, AI_RISK_HIGH, 300);
        assert(ok_et != NULL);
        csilk_json_add_string(ok_ea, "draft_token", ok_et);

        char* ok_er = ai_tool_dispatch_parsed(ed_def_ctx, "confirm_proposed_expense", ok_ea);
        assert(ok_er != NULL);
        csilk_json_t* ok_ep = csilk_json_parse(ok_er);
        assert(ok_ep != NULL && csilk_json_get_bool(ok_ep, "success"));
        int64_t ok_de_id = db_get_int(ok_ep, "id");
        assert(ok_de_id > 0);
        csilk_json_free(ok_ep);
        free(ok_er);
        free(ok_et);
        csilk_json_free(ok_ea);

        arw_row = asset_get_raw(pool, owner_id, asset_rw);
        assert(db_get_num(csilk_json_array_get(arw_row, 0), "current_value") == 940.0);
        csilk_json_free(arw_row);

        char de_id_s[32];
        snprintf(de_id_s, sizeof(de_id_s), "%lld", (long long)ok_de_id);
        csilk_json_t* de_chk = csilk_db_query_param_json(
            pool, "SELECT ledger_id FROM daily_expenses WHERE id=?", (const char*[]){de_id_s, NULL});
        assert(de_chk != NULL && csilk_json_array_size(de_chk) == 1);
        assert(db_get_int(csilk_json_array_get(de_chk, 0), "ledger_id") == sh_lid);
        csilk_json_free(de_chk);

        /* Positive transfer between operator assets asset_rw -> asset_rw2 with omitted/default metadata ledger_id */
        csilk_json_t* ok_ta = csilk_json_object();
        csilk_json_add_number(ok_ta, "amount", 150.0);
        csilk_json_add_number(ok_ta, "from_asset_id", (double)asset_rw);
        csilk_json_add_number(ok_ta, "to_asset_id", (double)asset_rw2);
        csilk_json_add_string(ok_ta, "date", "2026-11-20");
        csilk_json_add_string(ok_ta, "note", "Shared op transfer");
        char* ok_tt = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_transfer", ok_ta, AI_RISK_HIGH, 300);
        assert(ok_tt != NULL);
        csilk_json_add_string(ok_ta, "draft_token", ok_tt);

        char* ok_tr = ai_tool_dispatch_parsed(ed_def_ctx, "confirm_proposed_transfer", ok_ta);
        assert(ok_tr != NULL);
        csilk_json_t* ok_tp = csilk_json_parse(ok_tr);
        assert(ok_tp != NULL && csilk_json_get_bool(ok_tp, "success"));
        int64_t ok_tf_id = db_get_int(ok_tp, "id");
        assert(ok_tf_id > 0);
        csilk_json_free(ok_tp);
        free(ok_tr);
        free(ok_tt);
        csilk_json_free(ok_ta);

        arw_row = asset_get_raw(pool, owner_id, asset_rw);
        csilk_json_t* arw2_row = asset_get_raw(pool, owner_id, asset_rw2);
        assert(db_get_num(csilk_json_array_get(arw_row, 0), "current_value") == 790.0);
        assert(db_get_num(csilk_json_array_get(arw2_row, 0), "current_value") == 650.0);
        csilk_json_free(arw_row);
        csilk_json_free(arw2_row);

        char tf_id_s[32];
        snprintf(tf_id_s, sizeof(tf_id_s), "%lld", (long long)ok_tf_id);
        csilk_json_t* tf_chk = csilk_db_query_param_json(
            pool, "SELECT ledger_id, from_tx_id, to_tx_id FROM transfers WHERE id=?", (const char*[]){tf_id_s, NULL});
        assert(tf_chk != NULL && csilk_json_array_size(tf_chk) == 1);
        assert(db_get_int(csilk_json_array_get(tf_chk, 0), "ledger_id") == sh_lid);
        int64_t ok_ftx = db_get_int(csilk_json_array_get(tf_chk, 0), "from_tx_id");
        int64_t ok_ttx = db_get_int(csilk_json_array_get(tf_chk, 0), "to_tx_id");
        csilk_json_free(tf_chk);

        char ftx_buf[32], ttx_buf[32];
        snprintf(ftx_buf, sizeof(ftx_buf), "%lld", (long long)ok_ftx);
        snprintf(ttx_buf, sizeof(ttx_buf), "%lld", (long long)ok_ttx);
        csilk_json_t* tx_chk = csilk_db_query_param_json(
            pool, "SELECT ledger_id FROM transactions WHERE id IN (?, ?)", (const char*[]){ftx_buf, ttx_buf, NULL});
        assert(tx_chk != NULL && csilk_json_array_size(tx_chk) == 2);
        assert(db_get_int(csilk_json_array_get(tx_chk, 0), "ledger_id") == sh_lid);
        assert(db_get_int(csilk_json_array_get(tx_chk, 1), "ledger_id") == sh_lid);
        csilk_json_free(tx_chk);

        /* Explicit cross-ledger mismatch (ledger_id_explicit=true with personal_lid) -> rejected 1004 */
        csilk_json_add_bool(ed_def_ctx->metadata, "ledger_id_explicit", true);
        csilk_json_t* mis_ea = csilk_json_object();
        csilk_json_add_number(mis_ea, "amount", 10.0);
        csilk_json_add_string(mis_ea, "type", "expense");
        csilk_json_add_string(mis_ea, "date", "2026-11-20");
        csilk_json_add_number(mis_ea, "category_id", 20.0);
        csilk_json_add_number(mis_ea, "asset_id", (double)asset_rw);
        char* mis_et = ai_confirmation_create_bound_token(
            editor_id, 10, "confirm_proposed_expense", mis_ea, AI_RISK_HIGH, 300);
        csilk_json_add_string(mis_ea, "draft_token", mis_et);
        char* mis_er = ai_tool_dispatch_parsed(ed_def_ctx, "confirm_proposed_expense", mis_ea);
        csilk_json_t* mis_ep = csilk_json_parse(mis_er);
        assert(mis_ep != NULL && !csilk_json_get_bool(mis_ep, "success") && db_get_int(mis_ep, "code") == 1004);
        csilk_json_free(mis_ep);
        free(mis_er);
        free(mis_et);
        csilk_json_free(mis_ea);

        ai_tool_context_free(ed_def_ctx);
        printf("  ✅ Shared-ledger operator expense & transfer confirm updated balances & stamped ledger_id=%lld (ignoring default fallback)\n",
               (long long)sh_lid);
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
