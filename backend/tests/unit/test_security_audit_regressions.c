#include "common/http_safety.h"
#include "common/ai_trace.h"
#include "common/ai_config.h"
#include "common/db.h"
#include "common/ledger_utils.h"
#include "config/secret.h"
#include "core/ledger/ledger_types.h"
#include "domain/mcp/rules.h"
#include "domain/auth/repository.h"
#include "domain/ledger/entity.h"
#include "domain/ledger/repository.h"
#include "infrastructure/repositories/daily_expense_repo_impl.h"
#include "infrastructure/repositories/ledger_repo_impl.h"
#include "application/category/usecases.h"
#include "application/report/commands.h"
#include "application/task/async_task_manager.h"
#include "services/report_asset_service.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/context.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TEST_DB_PATH = "/tmp/test_mf_sec_audit_reg.db";

static void
cleanup_db_files(void)
{
    unlink(TEST_DB_PATH);
    char shm[256], wal[256];
    snprintf(shm, sizeof(shm), "%s-shm", TEST_DB_PATH);
    snprintf(wal, sizeof(wal), "%s-wal", TEST_DB_PATH);
    unlink(shm);
    unlink(wal);
}

static csilk_db_pool_t*
setup_test_db(void)
{
    cleanup_db_files();
    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_sec_regressions_32b");

    csilk_db_pool_t* pool = NULL;
    int              rc = db_init(&pool);
    assert(rc == 0 && pool != NULL);
    rc = db_run_migrations(pool);
    assert(rc == 0);
    return pool;
}

static void
test_ssrf_and_mcp_url_hardening(void)
{
    printf("[1] SSRF & MCP URL hardening...\n");

    /* Private/reserved IP literals and obfuscated forms must be rejected */
    assert(mf_http_host_is_private_literal("127.0.0.1") == true);
    assert(mf_http_host_is_private_literal("localhost") == true);
    assert(mf_http_host_is_private_literal("10.0.0.1") == true);
    assert(mf_http_host_is_private_literal("172.16.0.1") == true);
    assert(mf_http_host_is_private_literal("192.168.1.10") == true);
    assert(mf_http_host_is_private_literal("169.254.169.254") == true);
    assert(mf_http_host_is_private_literal("100.64.0.1") == true);
    assert(mf_http_host_is_private_literal("::1") == true);
    assert(mf_http_host_is_private_literal("::ffff:127.0.0.1") == true);
    assert(mf_http_host_is_private_literal("fc00::1") == true);
    assert(mf_http_host_is_private_literal("fe80::1") == true);
    assert(mf_http_host_is_private_literal("0x7f000001") == true);
    assert(mf_http_host_is_private_literal("0177.0.0.1") == true);
    assert(mf_http_host_is_private_literal("2130706433") == true);
    assert(mf_http_host_is_private_literal("8.8.8.8") == false);
    assert(mf_http_host_is_private_literal("1.1.1.1") == false);

    /* Cloud metadata must be blocked even when allow_local == true */
    char resolve_buf[320] = {0};
    assert(mf_http_validate_and_resolve_url(
               "http://169.254.169.254/latest/meta-data", 0, true, false, resolve_buf, sizeof(resolve_buf)) !=
           0);
    assert(mf_http_validate_and_resolve_url(
               "http://127.0.0.1:8080/api", 0, false, false, resolve_buf, sizeof(resolve_buf)) != 0);
    assert(mf_http_validate_and_resolve_url(
               "http://[::1]:8080/api", 0, false, false, resolve_buf, sizeof(resolve_buf)) != 0);
    assert(mf_http_validate_and_resolve_url(
               "http://0x7f000001/", 0, false, false, resolve_buf, sizeof(resolve_buf)) != 0);
    assert(mf_http_validate_and_resolve_url(
               "file:///etc/passwd", 0, false, false, resolve_buf, sizeof(resolve_buf)) != 0);
    assert(mf_http_validate_and_resolve_url(
               "http://user:pass@8.8.8.8/", 0, false, false, resolve_buf, sizeof(resolve_buf)) != 0);
    assert(mf_http_validate_and_resolve_url(
               "http://127.0.0.1:11434/api/chat", 0, true, false, resolve_buf, sizeof(resolve_buf)) ==
           0);

    /* MCP domain URL validator delegates to strict SSRF checks */
    assert(mf_mcp_rule_validate_url("http://169.254.169.254/latest/meta-data", true) == false);
    assert(mf_mcp_rule_validate_url("http://0x7f000001/mcp", false) == false);
    assert(mf_mcp_rule_validate_url("http://0177.0.0.1/mcp", false) == false);
    assert(mf_mcp_rule_validate_url("http://[::ffff:127.0.0.1]/mcp", false) == false);
    assert(mf_mcp_rule_validate_url("http://localhost:3000/mcp", false) == false);
    assert(mf_mcp_rule_validate_url("http://localhost:3000/mcp", true) == true);
    assert(mf_mcp_rule_validate_url("https://8.8.8.8/mcp", false) == true);
}

static void
test_ai_internal_only_gating(void)
{
    printf("[2] AI internal_only HITL gating...\n");
    ai_tool_registry_init();

    const ai_tool_t* t_exp = ai_tool_find("confirm_proposed_expense");
    const ai_tool_t* t_trf = ai_tool_find("confirm_proposed_transfer");
    assert(t_exp != NULL && t_exp->internal_only == true);
    assert(t_trf != NULL && t_trf->internal_only == true);

    /* internal_only tools must NEVER be exposed in LLM Function Calling definitions */
    size_t                 def_cnt = 0;
    const csilk_ai_tool_t* defs = ai_tool_get_csilk_definitions(&def_cnt);
    assert(defs != NULL && def_cnt > 0);
    for (size_t i = 0; i < def_cnt; i++) {
        assert(strcmp(defs[i].function.name, "confirm_proposed_expense") != 0);
        assert(strcmp(defs[i].function.name, "confirm_proposed_transfer") != 0);
    }

    /* Dispatcher must block model calls when ctx->forbid_internal is set */
    ai_tool_context_t* ctx = ai_tool_context_create(NULL, 1, 1, "trace-sec-test");
    assert(ctx != NULL);
    ctx->forbid_internal = true;

    char* res = ai_tool_dispatch(ctx, "confirm_proposed_expense", "{}");
    assert(res != NULL);
    assert(ai_tool_result_is_error(res) == true);
    assert(strstr(res, "internal_only_tool") != NULL);
    free(res);

    ai_tool_context_free(ctx);
}

static void
test_ledger_loss_and_trace_config_bounds(void)
{
    printf("[3] Ledger loss type, ai_trace JSON escaping, ai_config defensive parsing...\n");

    assert(ledger_tx_type_from_str("loss") == LEDGER_TX_LOSS);
    assert(strcmp(ledger_tx_type_to_str(LEDGER_TX_LOSS), "loss") == 0);

    ai_trace_t trace;
    ai_trace_init(&trace, 1, 2);
    for (int i = 0; i < 64; i++) {
        ai_trace_add_tool_span(&trace, "tool\"with\\quotes\nand\tctrl", 12, 34, 1);
    }
    assert(trace.tool_spans[0] == '[');
    csilk_json_t* parsed = csilk_json_parse(trace.tool_spans);
    assert(parsed != NULL && csilk_json_is_array(parsed));
    csilk_json_free(parsed);
    ai_trace_free(&trace);

    ai_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    int rc = ai_config_load_json("{\"providers\":[{\"id\":\"p1\",\"name\":\"Provider 1\"}]}", &cfg);
    assert(rc == 0);
    assert(cfg.provider_count == 1);
    assert(cfg.providers[0].model_count == 0);
    ai_config_free(&cfg);
}

static void
cleanup_dummy_arg(void* arg)
{
    int* cleaned = (int*)arg;
    if (cleaned) {
        *cleaned = 1;
    }
}

static int
dummy_async_fn(void*   pool,
               int64_t task_id,
               int64_t user_id,
               int64_t ledger_id,
               void*   arg,
               char**  out_result_json,
               char*   err_buf,
               size_t  err_cap)
{
    (void)pool;
    (void)task_id;
    (void)user_id;
    (void)ledger_id;
    (void)arg;
    (void)out_result_json;
    (void)err_buf;
    (void)err_cap;
    return 0;
}

static void
test_db_backed_regressions(csilk_db_pool_t* pool)
{
    printf("[4] HHI two-pass, daily expense tag_ids filter, atomic ledger delete, async shutdown...\n");

    int64_t user_id = 0;
    int     rc = mf_auth_repo_create(pool, "sec_reg_user", "hash_sec_reg_123456", &user_id);
    assert(rc == 0 && user_id > 0);

    char uid_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)user_id);
    csilk_db_exec_param(
        pool, "INSERT INTO ledgers (user_id, name, is_default) VALUES (?, '默认账本', 1);", (const char*[]){uid_s, NULL});
    int64_t ledger_id = ledger_get_default(pool, user_id);
    assert(ledger_id > 0);

    category_usecase_seed_defaults(pool, user_id, ledger_id);

    csilk_json_t* cats = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND type='asset' ORDER BY id ASC LIMIT 1;",
        (const char*[]){uid_s, NULL});
    assert(cats != NULL && csilk_json_array_size(cats) == 1);
    int64_t asset_cat_id = db_get_int(csilk_json_array_get(cats, 0), "id");
    csilk_json_free(cats);

    csilk_json_t* exp_cats = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND type='expense' ORDER BY id ASC LIMIT 1;",
        (const char*[]){uid_s, NULL});
    assert(exp_cats != NULL && csilk_json_array_size(exp_cats) == 1);
    int64_t exp_cat_id = db_get_int(csilk_json_array_get(exp_cats, 0), "id");
    csilk_json_free(exp_cats);

    char lid_s[32], acat_s[32];
    snprintf(lid_s, sizeof(lid_s), "%lld", (long long)ledger_id);
    snprintf(acat_s, sizeof(acat_s), "%lld", (long long)asset_cat_id);

    /* Two equal non-liability assets of 50,000 => weight = 0.5 each => HHI = 0.25 + 0.25 = 0.5 */
    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, "
                        "current_value, cost_basis, net_value) VALUES (?, ?, ?, 'Asset A', 'CNY', 50000.0, 50000.0, 50000.0);",
                        (const char*[]){uid_s, lid_s, acat_s, NULL});
    csilk_db_exec_param(pool,
                        "INSERT INTO assets (user_id, ledger_id, category_id, name, currency, "
                        "current_value, cost_basis, net_value) VALUES (?, ?, ?, 'Asset B', 'CNY', 50000.0, 50000.0, 50000.0);",
                        (const char*[]){uid_s, lid_s, acat_s, NULL});

    report_query_t rq = {
        .user_id = user_id,
        .ledger_id = ledger_id,
        .base_currency = "CNY",
    };
    report_result_t rres = {0};
    report_position_overview(pool, &rq, &rres);
    assert(rres.code == 0 && rres.data_payload != NULL);
    double hhi = csilk_json_get_number(rres.data_payload, "hhi");
    double max_w = csilk_json_get_number(rres.data_payload, "max_holding_weight");
    assert(fabs(hhi - 0.5) < 1e-4);
    assert(fabs(max_w - 0.5) < 1e-4);
    csilk_json_free(rres.data_payload);

    /* Daily expense tag_ids filter must restrict both rows and total count */
    csilk_json_t* assets = csilk_db_query_param_json(
        pool, "SELECT id FROM assets WHERE user_id=? ORDER BY id ASC LIMIT 1;", (const char*[]){uid_s, NULL});
    assert(assets != NULL && csilk_json_array_size(assets) == 1);
    int64_t asset_id = db_get_int(csilk_json_array_get(assets, 0), "id");
    csilk_json_free(assets);

    int64_t exp1 = mf_daily_expense_repo_insert(
        pool, user_id, ledger_id, exp_cat_id, asset_id, "expense", 10.0, "CNY", "2026-11-01", "tagged");
    int64_t exp2 = mf_daily_expense_repo_insert(
        pool, user_id, ledger_id, exp_cat_id, asset_id, "expense", 20.0, "CNY", "2026-11-02", "untagged");
    assert(exp1 > 0 && exp2 > 0);

    csilk_db_exec_param(
        pool, "INSERT INTO tags (user_id, name, color) VALUES (?, 'sec_tag', '#ff0000');", (const char*[]){uid_s, NULL});
    csilk_json_t* tag_rows = csilk_db_query_param_json(
        pool, "SELECT id FROM tags WHERE user_id=? AND name='sec_tag';", (const char*[]){uid_s, NULL});
    assert(tag_rows != NULL && csilk_json_array_size(tag_rows) == 1);
    int64_t tag_id = db_get_int(csilk_json_array_get(tag_rows, 0), "id");
    csilk_json_free(tag_rows);

    char exp1_s[32], tag_s[32];
    snprintf(exp1_s, sizeof(exp1_s), "%lld", (long long)exp1);
    snprintf(tag_s, sizeof(tag_s), "%lld", (long long)tag_id);
    csilk_db_exec_param(
        pool, "INSERT INTO expense_tags (expense_id, tag_id) VALUES (?, ?);", (const char*[]){exp1_s, tag_s, NULL});

    csilk_json_t* list_json = NULL;
    int64_t       total = -1;
    rc = mf_daily_expense_repo_list(
        pool, user_id, ledger_id, 1, 20, NULL, NULL, tag_s, NULL, NULL, &list_json, &total);
    assert(rc == 0 && total == 1 && list_json != NULL);
    assert(csilk_json_array_size(list_json) == 1);
    csilk_json_free(list_json);

    /* Secondary ledger create + atomic cascade delete */
    mf_ledger_t new_ledger = {.name = "Temp Secondary", .currency = "CNY"};
    int64_t     sec_lid = mf_ledger_repo_create(pool, user_id, &new_ledger);
    assert(sec_lid > 0);
    mf_ledger_t fetched = {0};
    assert(mf_ledger_repo_get(pool, sec_lid, &fetched) == 0);
    assert(mf_ledger_repo_delete(pool, sec_lid) == 0);
    assert(mf_ledger_repo_get(pool, sec_lid, &fetched) == 1);

    /* Async task manager rejects new tasks cleanly after shutdown and runs cleanup callback */
    assert(mf_async_task_manager_init(pool, 1) == 0);
    mf_async_task_manager_shutdown();
    int cleaned = 0;
    int64_t tid = mf_async_task_submit(
        pool, user_id, ledger_id, "test_job", NULL, "init", dummy_async_fn, &cleaned, cleanup_dummy_arg, NULL);
    assert(tid == -1 && cleaned == 1);
}

int
main(void)
{
    printf("=== Starting Security & Audit Regression Tests ===\n");
    test_ssrf_and_mcp_url_hardening();
    test_ai_internal_only_gating();
    test_ledger_loss_and_trace_config_bounds();

    csilk_db_pool_t* pool = setup_test_db();
    test_db_backed_regressions(pool);

    cleanup_db_files();
    printf("=== All Security & Audit Regression Tests Passed ===\n");
    return 0;
}
