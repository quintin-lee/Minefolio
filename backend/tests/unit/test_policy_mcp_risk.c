#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include "domain/auth/repository.h"
#include "domain/mcp/entity.h"
#include "domain/mcp/repository.h"
#include "services/ai/policy/permission.h"
#include "services/ai/policy/policy.h"
#include "services/ai/policy/risk.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/mcp/mcp_bridge.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_policy_mcp_risk.db";

static csilk_db_pool_t*
setup_test_db(void)
{
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_policy_mcp_risk_32b!");

    csilk_db_pool_t* pool = NULL;
    int              rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);

    rc = db_run_migrations(pool);
    assert(rc == 0);
    return pool;
}

int
main(void)
{
    /* Pin deterministic rules: confirmation enforced for medium+, no freq cap in the way. */
    ai_policy_rules_t rules = {
        .single_amount_limit = 500000.0,
        .large_amount_threshold = 50000.0,
        .max_frequency_per_minute = 60,
        .enforce_confirmation = true,
    };
    ai_policy_set_rules(&rules);
    ai_policy_reset_frequency_limits();

    /* A real user id so auth passes; tool name looks like a read tool by prefix
     * so the name-pattern fallback would compute a low/readonly risk. */
    const char* tool = "mcp:7:get_foo";

    /* No override -> today's name-pattern behavior (low-ish, not CRITICAL). */
    ai_policy_decision_t* d1 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_NO_OVERRIDE);
    assert(d1);
    assert(d1->risk_level < AI_RISK_CRITICAL);
    ai_policy_decision_free(d1);

    /* Override with HIGH -> the decision must carry HIGH and require confirmation
     * (enforce_confirmation is pinned true above). */
    ai_policy_decision_t* d2 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_HIGH);
    assert(d2);
    assert(d2->risk_level == AI_RISK_HIGH);
    assert(d2->requires_confirmation == true);
    ai_policy_decision_free(d2);

    /* Downgrade attempt: delete_asset is CRITICAL by default; overriding with
     * AI_RISK_LOW must NOT lower the risk level. */
    ai_policy_decision_t* d3 = ai_policy_evaluate(1, 0, "delete_asset", NULL, AI_RISK_LOW);
    assert(d3);
    assert(d3->risk_level == AI_RISK_CRITICAL);
    ai_policy_decision_free(d3);

    /* Downgrade attempt on an allowed confirmation tool must preserve HIGH and confirmation. */
    ai_permission_set_user_role(2, AI_USER_ROLE_ADMIN);
    ai_policy_decision_t* d4 =
        ai_policy_evaluate(2, 0, "confirm_proposed_transfer", NULL, AI_RISK_READ_ONLY);
    assert(d4);
    assert(d4->allowed == true);
    assert(d4->risk_level == AI_RISK_HIGH);
    assert(d4->requires_confirmation == true);
    ai_policy_decision_free(d4);

    /* Regression: policy-denied mcp_bridge_dispatch must copy decision->reason before
     * freeing decision (prevents heap use-after-free under ASan). */
    csilk_db_pool_t* pool = setup_test_db();
    int64_t          uid = 0;
    int              rc = mf_auth_repo_create(pool, "mcp_risk_tester", "hash_pw_123456", &uid);
    assert(rc == 0 && uid > 0);
    ai_permission_set_user_role(uid, AI_USER_ROLE_STANDARD);

    mf_mcp_server_t srv = {0};
    snprintf(srv.name, sizeof(srv.name), "risk_probe_srv");
    srv.transport = MCP_TRANSPORT_HTTP;
    snprintf(srv.url, sizeof(srv.url), "http://127.0.0.1:1/mcp");
    srv.enabled = true;
    srv.timeout_ms = 1000;
    int64_t server_id = 0;
    assert(mf_mcp_server_repo_create(pool, uid, &srv, &server_id) == 0 && server_id > 0);

    ai_tool_context_t* ctx = ai_tool_context_create(pool, uid, 0, "trace_mcp_risk");
    assert(ctx != NULL);
    ctx->permissions = (1u << AI_PERM_READ) | (1u << AI_PERM_WRITE);

    char qualified_tool[128];
    snprintf(qualified_tool, sizeof(qualified_tool), "mcp:%lld:do_transfer", (long long)server_id);

    csilk_json_t* args = csilk_json_object();
    csilk_json_add_number(args, "amount", 99999999.0); /* exceeds single_amount_limit */

    char* dispatch_out = mcp_bridge_dispatch(ctx, qualified_tool, args);
    assert(dispatch_out != NULL);

    csilk_json_t* parsed = csilk_json_parse(dispatch_out);
    assert(parsed != NULL);
    const char* err_str = csilk_json_get_string(parsed, "error");
    const char* reason_str = csilk_json_get_string(parsed, "reason");
    assert(err_str != NULL && strcmp(err_str, "policy_denied") == 0);
    assert(reason_str != NULL && reason_str[0] != '\0');

    csilk_json_free(parsed);
    free(dispatch_out);
    csilk_json_free(args);
    ai_tool_context_free(ctx);

    unlink(TEST_DB_PATH);
    {
        char shm[256], wal[256];
        snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
        snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
        unlink(shm);
        unlink(wal);
    }

    printf("PASS: test_policy_mcp_risk\n");
    return 0;
}
