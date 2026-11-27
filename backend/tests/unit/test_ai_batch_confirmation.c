#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/tools/batch_tool.h"
#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"
#include "application/account/usecases.h"
#include "core/financial/money.h"

static void
seed_test_user_and_assets(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t*         out_a1,
                          int64_t*         out_a2,
                          int64_t*         out_cat)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO users (id, username, password) VALUES (?, 'batch_user', 'hash')",
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
        "INSERT INTO categories (id, user_id, name, type, icon) VALUES (10, ?, '餐饮', "
        "'expense', '')",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, "
        "ledger_id) VALUES (101, ?, 10, 1, '现金钱包', 1000.0, 'CNY', 1)",
        (const char*[]){uid_str, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool,
        "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, "
        "ledger_id) VALUES (102, ?, 10, 1, '银行卡', 500.0, 'CNY', 1)",
        (const char*[]){uid_str, NULL}));

    if (out_a1) {
        *out_a1 = 101;
    }
    if (out_a2) {
        *out_a2 = 102;
    }
    if (out_cat) {
        *out_cat = 10;
    }
}

static double
get_asset_balance(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id)
{
    char uid_str[32], aid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT current_value FROM assets WHERE id=? AND user_id=?",
        (const char*[]){aid_str, uid_str, NULL});
    double val = 0.0;
    if (res && csilk_json_array_size(res) > 0) {
        val = db_get_num(csilk_json_array_get(res, 0), "current_value");
    }
    if (res) {
        csilk_json_free(res);
    }
    return val;
}

static csilk_json_t*
create_sample_actions_a(void)
{
    csilk_json_t* arr = csilk_json_array();

    csilk_json_t* act1 = csilk_json_object();
    csilk_json_add_string(act1, "type", "daily_expense");
    csilk_json_add_number(act1, "amount", 45.0);
    csilk_json_add_number(act1, "category_id", 3.0);
    csilk_json_add_number(act1, "asset_id", 1.0);
    csilk_json_add_string(act1, "note", "Lunch");
    csilk_json_array_append(arr, act1);

    csilk_json_t* act2 = csilk_json_object();
    csilk_json_add_string(act2, "type", "transfer");
    csilk_json_add_number(act2, "amount", 200.0);
    csilk_json_add_number(act2, "from_asset_id", 1.0);
    csilk_json_add_number(act2, "to_asset_id", 2.0);
    csilk_json_add_string(act2, "note", "Savings");
    csilk_json_array_append(arr, act2);

    return arr;
}

static csilk_json_t*
create_sample_actions_b(void)
{
    /* Identical content to A, newly allocated */
    return create_sample_actions_a();
}

int
main(int argc, char** argv)
{
    if (argc >= 4 && strcmp(argv[1], "--sign") == 0) {
        int64_t       user_id = atoll(argv[2]);
        const char*   json_str = argv[3];
        csilk_json_t* actions = csilk_json_parse(json_str);
        if (!actions) {
            fprintf(stderr, "Invalid actions JSON\n");
            return 1;
        }
        char* token = ai_confirmation_create_batch_token(user_id, 0, actions, AI_RISK_HIGH, 300);
        if (!token) {
            fprintf(stderr, "Failed to create batch token\n");
            csilk_json_free(actions);
            return 1;
        }
        printf("%s\n", token);
        free(token);
        csilk_json_free(actions);
        return 0;
    }

    printf("============================================================\n");
    printf("   TEST SUITE: AI Batch Confirmation Protocol (mf_batch_v1)  \n");
    printf("============================================================\n");

    /* ------------------------------------------------------------- */
    /* Test 1: Batch Hash Determinism Across Multiple Actions        */
    /* ------------------------------------------------------------- */
    printf("[Test 1] Batch Hash Determinism Across Multiple Actions...\n");
    {
        csilk_json_t* actions_a = create_sample_actions_a();
        csilk_json_t* actions_b = create_sample_actions_b();

        char hash_a[65] = {0};
        char hash_b[65] = {0};
        ai_confirmation_hash_batch_actions(actions_a, hash_a, sizeof(hash_a));
        ai_confirmation_hash_batch_actions(actions_b, hash_b, sizeof(hash_b));

        assert(strlen(hash_a) == 64);
        assert(strlen(hash_b) == 64);
        assert(strcmp(hash_a, hash_b) == 0);
        printf("  ✅ Identical batch arrays produce identical composite hash: %s\n", hash_a);

        /* Alter amount in action 1 */
        csilk_json_t* act1_mod = csilk_json_array_get(actions_b, 0);
        csilk_json_add_number(act1_mod, "amount", 45.5);
        char hash_b_mod[65] = {0};
        ai_confirmation_hash_batch_actions(actions_b, hash_b_mod, sizeof(hash_b_mod));
        assert(strcmp(hash_a, hash_b_mod) != 0);
        printf("  ✅ Modifying amount in action 1 changes hash: %s\n", hash_b_mod);

        /* Revert and alter note in action 2 */
        csilk_json_add_number(act1_mod, "amount", 45.0);
        csilk_json_t* act2_mod = csilk_json_array_get(actions_b, 1);
        csilk_json_add_string(act2_mod, "note", "Emergency");
        char hash_b_mod2[65] = {0};
        ai_confirmation_hash_batch_actions(actions_b, hash_b_mod2, sizeof(hash_b_mod2));
        assert(strcmp(hash_a, hash_b_mod2) != 0);
        printf("  ✅ Modifying note in action 2 changes hash: %s\n", hash_b_mod2);

        /* Reordering actions alters composite hash */
        csilk_json_t* actions_reordered = csilk_json_array();
        csilk_json_array_append(actions_reordered,
                                csilk_json_copy(csilk_json_array_get(actions_a, 1)));
        csilk_json_array_append(actions_reordered,
                                csilk_json_copy(csilk_json_array_get(actions_a, 0)));
        char hash_reordered[65] = {0};
        ai_confirmation_hash_batch_actions(
            actions_reordered, hash_reordered, sizeof(hash_reordered));
        assert(strcmp(hash_a, hash_reordered) != 0);
        printf("  ✅ Reordering actions changes composite hash: %s\n", hash_reordered);

        /* Removing an action alters hash */
        csilk_json_t* actions_single = csilk_json_array();
        csilk_json_array_append(actions_single,
                                csilk_json_copy(csilk_json_array_get(actions_a, 0)));
        char hash_single[65] = {0};
        ai_confirmation_hash_batch_actions(actions_single, hash_single, sizeof(hash_single));
        assert(strcmp(hash_a, hash_single) != 0);
        printf("  ✅ Removing an action changes composite hash: %s\n", hash_single);

        csilk_json_free(actions_a);
        csilk_json_free(actions_b);
        csilk_json_free(actions_reordered);
        csilk_json_free(actions_single);
    }

    /* ------------------------------------------------------------- */
    /* Test 2: Token Creation and Successful Single Verification      */
    /* ------------------------------------------------------------- */
    printf("[Test 2] Token Creation & Successful Verification...\n");
    {
        ai_confirmation_set_secret("test_batch_confirmation_secret_32bytes!");
        ai_confirmation_reset_nonce_cache();

        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);
        assert(strncmp(token, "mf_batch_v1.", 12) == 0);
        printf("  ✅ Generated batch token with prefix mf_batch_v1: %s\n", token);

        ai_confirmation_status_t st =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st == AI_CONFIRM_OK);
        printf("  ✅ Verified and consumed successfully: %s\n", ai_confirmation_strerror(st));

        /* ------------------------------------------------------------- */
        /* Test 3: Anti-Replay Attack Prevention                         */
        /* ------------------------------------------------------------- */
        printf("[Test 3] Anti-Replay Attack Prevention...\n");
        ai_confirmation_status_t st_replay =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st_replay == AI_CONFIRM_ERR_REPLAY);
        printf("  ✅ Replay attack blocked: %s\n", ai_confirmation_strerror(st_replay));

        free(token);
        csilk_json_free(actions);
    }

    /* ------------------------------------------------------------- */
    /* Test 4: Tampering Detection (AI_CONFIRM_ERR_ARGS_MISMATCH)    */
    /* ------------------------------------------------------------- */
    printf("[Test 4] Tampering Detection...\n");
    {
        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);

        /* 4.1 Tamper amount in action 2 */
        csilk_json_t* tampered_actions = create_sample_actions_a();
        csilk_json_t* act2 = csilk_json_array_get(tampered_actions, 1);
        csilk_json_add_number(act2, "amount", 9999.0);

        ai_confirmation_status_t st_tamper =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, tampered_actions, token);
        assert(st_tamper == AI_CONFIRM_ERR_ARGS_MISMATCH);
        printf("  ✅ Tampered amount blocked: %s\n", ai_confirmation_strerror(st_tamper));

        /* 4.2 Tamper by appending an extra action */
        csilk_json_t* extra_act = csilk_json_object();
        csilk_json_add_string(extra_act, "type", "daily_expense");
        csilk_json_add_number(extra_act, "amount", 10.0);
        csilk_json_array_append(tampered_actions, extra_act);

        ai_confirmation_status_t st_extra =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, tampered_actions, token);
        assert(st_extra == AI_CONFIRM_ERR_ARGS_MISMATCH);
        printf("  ✅ Appended action blocked: %s\n", ai_confirmation_strerror(st_extra));

        csilk_json_free(tampered_actions);
        csilk_json_free(actions);
        free(token);
    }

    /* ------------------------------------------------------------- */
    /* Test 5: User Isolation (AI_CONFIRM_ERR_USER_MISMATCH)         */
    /* ------------------------------------------------------------- */
    printf("[Test 5] User Isolation...\n");
    {
        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);

        int64_t                  attacker_uid = 9999;
        ai_confirmation_status_t st_user =
            ai_confirmation_verify_and_consume_batch(attacker_uid, session_id, actions, token);
        assert(st_user == AI_CONFIRM_ERR_USER_MISMATCH);
        printf("  ✅ Cross-user attack blocked: %s\n", ai_confirmation_strerror(st_user));

        free(token);
        csilk_json_free(actions);
    }

    /* ------------------------------------------------------------- */
    /* Test 6: Expired Token Rejection (AI_CONFIRM_ERR_EXPIRED)      */
    /* ------------------------------------------------------------- */
    printf("[Test 6] Expired Token Rejection...\n");
    {
        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        /* Create token with TTL = -10 (already expired) */
        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, -10);
        assert(token != NULL);

        ai_confirmation_status_t st_exp =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st_exp == AI_CONFIRM_ERR_EXPIRED);
        printf("  ✅ Expired token blocked: %s\n", ai_confirmation_strerror(st_exp));

        free(token);
        csilk_json_free(actions);
    }

    /* ------------------------------------------------------------- */
    /* Test 7: Signature Tampering & Format Errors                   */
    /* ------------------------------------------------------------- */
    printf("[Test 7] Signature Tampering & Format Errors...\n");
    {
        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);

        /* Flip the last character in the HMAC signature */
        size_t len = strlen(token);
        token[len - 1] = (token[len - 1] == 'a') ? 'b' : 'a';

        ai_confirmation_status_t st_sig =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st_sig == AI_CONFIRM_ERR_SIGNATURE);
        printf("  ✅ Forged signature blocked: %s\n", ai_confirmation_strerror(st_sig));

        /* Invalid format prefix */
        ai_confirmation_status_t st_fmt = ai_confirmation_verify_and_consume_batch(
            user_id, session_id, actions, "invalid.format.token");
        assert(st_fmt == AI_CONFIRM_ERR_FORMAT);
        printf("  ✅ Invalid format token blocked: %s\n", ai_confirmation_strerror(st_fmt));

        free(token);
        csilk_json_free(actions);
    }

    /* ------------------------------------------------------------- */
    /* Test 8: Cross-Restart Anti-Replay with Database Nonce Ledger  */
    /* ------------------------------------------------------------- */
    printf("[Test 8] Cross-Restart Anti-Replay with Database Nonce Ledger...\n");
    {
        const char* test_db = "/tmp/test_ai_batch_confirmation.db";
        unlink(test_db);
        setenv("MINEFOLIO_DB_DSN", test_db, 1);
        setenv("DB_DRIVER", "sqlite", 1);

        csilk_db_pool_t* pool = NULL;
        assert(db_init(&pool) == 0 && pool != NULL);
        assert(db_run_migrations(pool) == 0);

        int64_t       user_id = 1001;
        int64_t       session_id = 42;
        csilk_json_t* actions = create_sample_actions_a();

        char* token =
            ai_confirmation_create_batch_token(user_id, session_id, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);

        /* First verification succeeds */
        ai_confirmation_status_t st1 =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st1 == AI_CONFIRM_OK);

        /* Reset in-memory cache to simulate server restart */
        ai_confirmation_reset_nonce_cache();

        /* Second verification must be caught by persistent database table */
        ai_confirmation_status_t st2 =
            ai_confirmation_verify_and_consume_batch(user_id, session_id, actions, token);
        assert(st2 == AI_CONFIRM_ERR_REPLAY);
        printf("  ✅ Persistent DB replay detection verified: %s\n", ai_confirmation_strerror(st2));

        free(token);
        csilk_json_free(actions);
        csilk_db_pool_free(pool);
        unlink(test_db);
    }

    /* ------------------------------------------------------------- */
    /* Test 9: Atomic Batch Confirmation Execution                  */
    /* ------------------------------------------------------------- */
    printf("[Test 9] Atomic Batch Confirmation Execution...\n");
    {
        const char* test_db = "/tmp/test_ai_batch_exec.db";
        unlink(test_db);
        setenv("MINEFOLIO_DB_DSN", test_db, 1);
        setenv("DB_DRIVER", "sqlite", 1);

        csilk_db_pool_t* pool = NULL;
        assert(db_init(&pool) == 0 && pool != NULL);
        assert(db_run_migrations(pool) == 0);

        int64_t user_id = 2001;
        int64_t a1 = 0, a2 = 0, cat = 0;
        seed_test_user_and_assets(pool, user_id, &a1, &a2, &cat);

        assert(get_asset_balance(pool, user_id, a1) == 1000.0);
        assert(get_asset_balance(pool, user_id, a2) == 500.0);

        /* Construct batch: 1 expense (50 CNY from a1) + 1 transfer (200 CNY from a1 to a2) */
        csilk_json_t* actions = csilk_json_array();
        csilk_json_t* act1 = csilk_json_object();
        csilk_json_add_string(act1, "action_type", "daily_expense");
        csilk_json_add_number(act1, "amount", 50.0);
        csilk_json_add_string(act1, "type", "expense");
        csilk_json_add_number(act1, "category_id", (double)cat);
        csilk_json_add_number(act1, "asset_id", (double)a1);
        csilk_json_add_string(act1, "note", "Lunch");
        csilk_json_array_append(actions, act1);

        csilk_json_t* act2 = csilk_json_object();
        csilk_json_add_string(act2, "action_type", "transfer");
        csilk_json_add_number(act2, "amount", 200.0);
        csilk_json_add_number(act2, "from_asset_id", (double)a1);
        csilk_json_add_number(act2, "to_asset_id", (double)a2);
        csilk_json_add_string(act2, "note", "Pocket money");
        csilk_json_array_append(actions, act2);

        char* token = ai_confirmation_create_batch_token(user_id, 0, actions, AI_RISK_HIGH, 300);
        assert(token != NULL);

        csilk_json_t* out_data = NULL;
        int           code = 0;
        char          err_buf[256] = {0};
        int           rc = ai_batch_actions_execute(
            pool, user_id, actions, token, &out_data, &code, err_buf, sizeof(err_buf));
        assert(rc == 0);
        assert(code == 0);
        assert(out_data != NULL);
        assert(db_get_num(out_data, "executed_count") == 2.0);
        csilk_json_free(out_data);

        /* Verify balances: a1 = 1000 - 50 - 200 = 750, a2 = 500 + 200 = 700 */
        double bal1 = get_asset_balance(pool, user_id, a1);
        double bal2 = get_asset_balance(pool, user_id, a2);
        assert(bal1 == 750.0);
        assert(bal2 == 700.0);
        printf("  ✅ Batch execution verified: a1 balance=%.2f, a2 balance=%.2f\n", bal1, bal2);

        /* Verify V014 transfer pairing created by batch transfer */
        {
            char uid_str[32];
            snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
            csilk_json_t* tf_rows = csilk_db_query_param_json(
                pool,
                "SELECT id, from_tx_id, to_tx_id FROM transfers WHERE user_id=? ORDER BY id DESC LIMIT 1",
                (const char*[]){uid_str, NULL});
            assert(tf_rows != NULL && csilk_json_array_size(tf_rows) == 1);
            int64_t ftx = db_get_int(csilk_json_array_get(tf_rows, 0), "from_tx_id");
            int64_t ttx = db_get_int(csilk_json_array_get(tf_rows, 0), "to_tx_id");
            assert(ftx > 0 && ttx > 0);
            csilk_json_free(tf_rows);

            char ftx_s[32], ttx_s[32];
            snprintf(ftx_s, sizeof(ftx_s), "%lld", (long long)ftx);
            snprintf(ttx_s, sizeof(ttx_s), "%lld", (long long)ttx);
            csilk_json_t* out_r = csilk_db_query_param_json(
                pool,
                "SELECT transaction_type, asset_id, linked_asset_id FROM transactions WHERE id=?",
                (const char*[]){ftx_s, NULL});
            csilk_json_t* in_r = csilk_db_query_param_json(
                pool,
                "SELECT transaction_type, asset_id, linked_asset_id, parent_tx_id FROM transactions WHERE id=?",
                (const char*[]){ttx_s, NULL});
            assert(out_r != NULL && csilk_json_array_size(out_r) == 1);
            assert(in_r != NULL && csilk_json_array_size(in_r) == 1);
            assert(strcmp(csilk_json_get_string(csilk_json_array_get(out_r, 0), "transaction_type"), "transfer_out") == 0);
            assert(strcmp(csilk_json_get_string(csilk_json_array_get(in_r, 0), "transaction_type"), "transfer_in") == 0);
            assert(db_get_int(csilk_json_array_get(in_r, 0), "parent_tx_id") == ftx);
            csilk_json_free(out_r);
            csilk_json_free(in_r);
            printf("  ✅ Batch transfer V014 pairing verified (from_tx_id=%lld, to_tx_id=%lld)\n",
                   (long long)ftx,
                   (long long)ttx);
        }

        /* ------------------------------------------------------------- */
        /* Test 11: Anti-Replay on Execution                            */
        /* ------------------------------------------------------------- */
        printf("[Test 11] Anti-Replay on Execution...\n");
        {
            csilk_json_t* out_data_replay = NULL;
            int           code_replay = 0;
            char          err_replay[256] = {0};
            int           rc_replay = ai_batch_actions_execute(pool,
                                                     user_id,
                                                     actions,
                                                     token,
                                                     &out_data_replay,
                                                     &code_replay,
                                                     err_replay,
                                                     sizeof(err_replay));
            assert(rc_replay < 0);
            assert(code_replay == 1004);
            printf("  ✅ Replay rejected with code %d: %s\n", code_replay, err_replay);
        }

        /* ------------------------------------------------------------- */
        /* Test 12: Tampering Detection on Execution                     */
        /* ------------------------------------------------------------- */
        printf("[Test 12] Tampering Detection on Execution...\n");
        {
            char* fresh_token =
                ai_confirmation_create_batch_token(user_id, 0, actions, AI_RISK_HIGH, 300);
            assert(fresh_token != NULL);

            /* Modify amount before executing */
            csilk_json_t* act1_mod = csilk_json_array_get(actions, 0);
            csilk_json_add_number(act1_mod, "amount", 999.0);

            csilk_json_t* out_data_tamper = NULL;
            int           code_tamper = 0;
            char          err_tamper[256] = {0};
            int           rc_tamper = ai_batch_actions_execute(pool,
                                                     user_id,
                                                     actions,
                                                     fresh_token,
                                                     &out_data_tamper,
                                                     &code_tamper,
                                                     err_tamper,
                                                     sizeof(err_tamper));
            assert(rc_tamper < 0);
            assert(code_tamper == 1002);
            printf("  ✅ Tampering rejected with code %d: %s\n", code_tamper, err_tamper);

            free(fresh_token);
        }

        /* ------------------------------------------------------------- */
        /* Test 10: Atomic Rollback on Partial Failure                  */
        /* ------------------------------------------------------------- */
        printf("[Test 10] Atomic Rollback on Partial Failure...\n");
        {
            csilk_json_t* bad_batch = csilk_json_array();
            csilk_json_t* b_act1 = csilk_json_object();
            csilk_json_add_string(b_act1, "action_type", "daily_expense");
            csilk_json_add_number(b_act1, "amount", 100.0);
            csilk_json_add_number(b_act1, "category_id", (double)cat);
            csilk_json_add_number(b_act1, "asset_id", (double)a1);
            csilk_json_add_string(b_act1, "note", "Dinner");
            csilk_json_array_append(bad_batch, b_act1);

            /* Action 2 has non-existent target asset ID */
            csilk_json_t* b_act2 = csilk_json_object();
            csilk_json_add_string(b_act2, "action_type", "transfer");
            csilk_json_add_number(b_act2, "amount", 50.0);
            csilk_json_add_number(b_act2, "from_asset_id", (double)a1);
            csilk_json_add_number(b_act2, "to_asset_id", 999999.0);
            csilk_json_add_string(b_act2, "note", "Bad transfer");
            csilk_json_array_append(bad_batch, b_act2);

            char* bad_token =
                ai_confirmation_create_batch_token(user_id, 0, bad_batch, AI_RISK_HIGH, 300);
            assert(bad_token != NULL);

            csilk_json_t* out_bad = NULL;
            int           code_bad = 0;
            char          err_bad[256] = {0};
            int           rc_bad = ai_batch_actions_execute(pool,
                                                  user_id,
                                                  bad_batch,
                                                  bad_token,
                                                  &out_bad,
                                                  &code_bad,
                                                  err_bad,
                                                  sizeof(err_bad));
            assert(rc_bad < 0);
            printf("  ✅ Batch failure caught: %s\n", err_bad);

            /* Verify rollback: Asset 1 balance must STILL be 750 (Action 1 rolled back!) */
            double bal1_post = get_asset_balance(pool, user_id, a1);
            assert(bal1_post == 750.0);
            printf("  ✅ Atomic rollback verified: a1 balance retained at %.2f\n", bal1_post);

            free(bad_token);
            csilk_json_free(bad_batch);
        }

        /* Cross-session rejection (tok_sid == session_id strong binding) */
        {
            char* sid_token =
                ai_confirmation_create_batch_token(user_id, 42, actions, AI_RISK_HIGH, 300);
            assert(sid_token != NULL);
            ai_confirmation_status_t st_cross =
                ai_confirmation_verify_and_consume_batch(user_id, 99, actions, sid_token);
            assert(st_cross == AI_CONFIRM_ERR_SESSION_MISMATCH);
            ai_confirmation_status_t st_zero =
                ai_confirmation_verify_and_consume_batch(user_id, 0, actions, sid_token);
            assert(st_zero == AI_CONFIRM_ERR_SESSION_MISMATCH);
            ai_confirmation_status_t st_match =
                ai_confirmation_verify_and_consume_batch(user_id, 42, actions, sid_token);
            assert(st_match == AI_CONFIRM_OK);
            free(sid_token);
            printf("  ✅ Cross-session batch token strongly rejected (AI_CONFIRM_ERR_SESSION_MISMATCH)\n");
        }

        /* ------------------------------------------------------------- */
        /* Test 13: Backdated Batch Expense Invalidates Snapshots From Date */
        /* ------------------------------------------------------------- */
        printf("[Test 13] Backdated batch expense invalidates ledger_snapshots from date...\n");
        {
            money_t sb;
            money_from_double(750.0, CURRENCY_CNY, &sb);
            assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-03", NULL, sb, "2026-11-03", 1, NULL) == 0);
            assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-12", NULL, sb, "2026-11-12", 2, NULL) == 0);
            assert(ledger_snapshot_save(pool, user_id, a1, "2026-11-19", NULL, sb, "2026-11-19", 3, NULL) == 0);

            csilk_json_t* bd_acts = csilk_json_array();
            csilk_json_t* bd_item = csilk_json_object();
            csilk_json_add_string(bd_item, "action_type", "daily_expense");
            csilk_json_add_number(bd_item, "amount", 25.0);
            csilk_json_add_string(bd_item, "type", "expense");
            csilk_json_add_number(bd_item, "category_id", (double)cat);
            csilk_json_add_number(bd_item, "asset_id", (double)a1);
            csilk_json_add_string(bd_item, "date", "2026-11-10");
            csilk_json_add_string(bd_item, "note", "Backdated batch expense");
            csilk_json_array_append(bd_acts, bd_item);

            char* bd_tok =
                ai_confirmation_create_batch_token(user_id, 0, bd_acts, AI_RISK_HIGH, 300);
            assert(bd_tok != NULL);

            csilk_json_t* bd_out = NULL;
            int           bd_code = 0;
            char          bd_err[256] = {0};
            assert(ai_batch_actions_execute(
                       pool, user_id, bd_acts, bd_tok, &bd_out, &bd_code, bd_err, sizeof(bd_err)) == 0);
            csilk_json_free(bd_out);
            free(bd_tok);
            csilk_json_free(bd_acts);

            csilk_json_t* snap = NULL;
            assert(ledger_snapshot_get_latest(pool, user_id, a1, "2026-11-25", &snap) == 0);
            assert(snap != NULL);
            assert(strcmp(csilk_json_get_string(snap, "snapshot_date"), "2026-11-03") == 0);
            csilk_json_free(snap);
            printf("  ✅ Backdated batch expense invalidated snapshots >= 2026-11-10\n");
        }

        /* ------------------------------------------------------------- */
        /* Test 14: Shared-ledger editor with viewer perm rejected (1004) */
        /* ------------------------------------------------------------- */
        printf("[Test 14] Shared-ledger editor with viewer permission rejected (1004) in batch...\n");
        {
            int64_t sh_owner = 2002;
            int64_t sh_editor = 2003;
            int64_t sh_lid = 2;
            int64_t acct_ro = 20;
            int64_t acct_rw = 21;
            int64_t asset_ro = 201;
            int64_t asset_rw = 202;

            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO users (id, username, password) VALUES (2002, 'b_owner', 'h'), (2003, 'b_editor', 'h')",
                (const char*[]){NULL}));
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (2, 2002, 'SharedBatch', 0)",
                (const char*[]){NULL}));
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (2, 2002, 'owner'), (2, 2003, 'editor')",
                (const char*[]){NULL}));
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO accounts (id, user_id, ledger_id, name, institution) VALUES "
                "(20, 2002, 2, 'ReadOnlyAcct', 'bank_card'), (21, 2002, 2, 'OperAcct', 'bank_card')",
                (const char*[]){NULL}));
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO categories (id, user_id, ledger_id, name, type, icon) VALUES (20, 2002, 2, 'Dining', 'expense', '')",
                (const char*[]){NULL}));
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "INSERT INTO assets (id, user_id, category_id, account_id, name, current_value, currency, ledger_id) VALUES "
                "(201, 2002, 20, 20, 'AssetRO', 1000.0, 'CNY', 2), "
                "(202, 2002, 20, 21, 'AssetRW', 500.0, 'CNY', 2)",
                (const char*[]){NULL}));

            account_usecase_result_t ares = {0};
            assert(account_permission_usecase_set(pool, sh_owner, sh_lid, acct_ro, sh_editor, "viewer", 0, 0, &ares) == 0);
            assert(account_permission_usecase_set(pool, sh_owner, sh_lid, acct_rw, sh_editor, "operator", 0, 0, &ares) == 0);

            /* 14a: Expense on viewer account rejected with 1004 */
            csilk_json_t* rb_exp = csilk_json_array();
            csilk_json_t* re1 = csilk_json_object();
            csilk_json_add_string(re1, "action_type", "daily_expense");
            csilk_json_add_number(re1, "amount", 40.0);
            csilk_json_add_string(re1, "type", "expense");
            csilk_json_add_number(re1, "category_id", 20.0);
            csilk_json_add_number(re1, "asset_id", (double)asset_ro);
            csilk_json_add_string(re1, "note", "Forbidden batch expense");
            csilk_json_array_append(rb_exp, re1);

            char* rb_tok1 =
                ai_confirmation_create_batch_token(sh_editor, 11, rb_exp, AI_RISK_HIGH, 300);
            csilk_json_t* rb_out1 = NULL;
            int           rb_code1 = 0;
            char          rb_err1[256] = {0};
            int           rb_rc1 = ai_batch_actions_execute_for_session(
                pool, sh_editor, 11, rb_exp, rb_tok1, &rb_out1, &rb_code1, rb_err1, sizeof(rb_err1));
            assert(rb_rc1 < 0);
            assert(rb_code1 == 1004);
            free(rb_tok1);
            csilk_json_free(rb_exp);
            assert(get_asset_balance(pool, sh_owner, asset_ro) == 1000.0);

            /* 14b: Allowed expense on asset_rw + forbidden transfer to asset_ro -> 1004 and atomic rollback */
            csilk_json_t* rb_mix = csilk_json_array();
            csilk_json_t* rm1 = csilk_json_object();
            csilk_json_add_string(rm1, "action_type", "daily_expense");
            csilk_json_add_number(rm1, "amount", 50.0);
            csilk_json_add_string(rm1, "type", "expense");
            csilk_json_add_number(rm1, "category_id", 20.0);
            csilk_json_add_number(rm1, "asset_id", (double)asset_rw);
            csilk_json_add_string(rm1, "note", "Allowed item before forbidden transfer");
            csilk_json_array_append(rb_mix, rm1);

            csilk_json_t* rm2 = csilk_json_object();
            csilk_json_add_string(rm2, "action_type", "transfer");
            csilk_json_add_number(rm2, "amount", 100.0);
            csilk_json_add_number(rm2, "from_asset_id", (double)asset_rw);
            csilk_json_add_number(rm2, "to_asset_id", (double)asset_ro);
            csilk_json_add_string(rm2, "note", "Forbidden transfer to RO");
            csilk_json_array_append(rb_mix, rm2);

            char* rb_tok2 =
                ai_confirmation_create_batch_token(sh_editor, 12, rb_mix, AI_RISK_HIGH, 300);
            csilk_json_t* rb_out2 = NULL;
            int           rb_code2 = 0;
            char          rb_err2[256] = {0};
            int           rb_rc2 = ai_batch_actions_execute_for_session(
                pool, sh_editor, 12, rb_mix, rb_tok2, &rb_out2, &rb_code2, rb_err2, sizeof(rb_err2));
            assert(rb_rc2 < 0);
            assert(rb_code2 == 1004);
            free(rb_tok2);
            csilk_json_free(rb_mix);

            assert(get_asset_balance(pool, sh_owner, asset_ro) == 1000.0);
            assert(get_asset_balance(pool, sh_owner, asset_rw) == 500.0);
            printf("  ✅ Shared-ledger editor with viewer perm rejected (1004) with atomic rollback in batch\n");
        }

        free(token);
        csilk_json_free(actions);
        csilk_db_pool_free(pool);
        unlink(test_db);
    }

    printf("============================================================\n");
    printf("   ALL 12 BATCH CONFIRMATION TESTS PASSED SUCCESSFULLY!      \n");
    printf("============================================================\n");
    return 0;
}
