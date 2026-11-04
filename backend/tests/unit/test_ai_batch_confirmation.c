#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/tools/batch_tool.h"

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
main(void)
{
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
            csilk_json_add_number(act1, "amount", 999.0);

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

        free(token);
        csilk_json_free(actions);
        unlink(test_db);
    }

    printf("============================================================\n");
    printf("   ALL 12 BATCH CONFIRMATION TESTS PASSED SUCCESSFULLY!      \n");
    printf("============================================================\n");
    return 0;
}
