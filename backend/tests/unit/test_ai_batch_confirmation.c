#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "services/ai/policy/confirmation.h"

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

    printf("============================================================\n");
    printf("   ALL 8 BATCH CONFIRMATION TESTS PASSED SUCCESSFULLY!       \n");
    printf("============================================================\n");
    return 0;
}
