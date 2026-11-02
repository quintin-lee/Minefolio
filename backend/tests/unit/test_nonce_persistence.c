#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common/db.h"
#include "services/ai/policy/confirmation.h"

int main(void) {
    printf("Running test_nonce_persistence...\n");
    unlink("/tmp/test_nonce_persistence.db");
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_nonce_persistence.db", 1);
    setenv("DB_DRIVER", "sqlite", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* Initialize secret */
    ai_confirmation_set_secret("test_secret_32bytes_ai_confirmation!");

    /* 1. Create a bound token */
    csilk_json_t* args = csilk_json_object();
    csilk_json_add_string(args, "target", "test_asset");
    char* token = ai_confirmation_create_bound_token(1, 1, "test_action", args, AI_RISK_HIGH, 60);
    assert(token != NULL);

    /* 2. First verification: must succeed with AI_CONFIRM_OK (0) */
    assert(ai_confirmation_verify_and_consume(1, 1, "test_action", args, token) == AI_CONFIRM_OK);

    /* 3. Reset in-memory ring cache to simulate daemon restart */
    ai_confirmation_reset_nonce_cache();

    /* 4. Second verification: must fail with AI_CONFIRM_ERR_REPLAY via database check */
    assert(ai_confirmation_verify_and_consume(1, 1, "test_action", args, token) == AI_CONFIRM_ERR_REPLAY);

    csilk_json_free(args);
    free(token);
    unlink("/tmp/test_nonce_persistence.db");
    printf("PASS: test_nonce_persistence completed successfully\n");
    return 0;
}
