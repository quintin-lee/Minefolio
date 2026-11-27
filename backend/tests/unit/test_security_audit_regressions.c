#include "common/http_safety.h"
#include "common/config.h"
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
#include "application/mcp/usecases.h"
#include "application/report/commands.h"
#include "application/task/async_task_manager.h"
#include "services/report_asset_service.h"
#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/mcp/mcp_client.h"
#include "csilk/csilk.h"
#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

    /* MCP secret_ref allowlist & admin-only RBAC on create/update */
    assert(mf_auth_repo_is_instance_admin(pool, user_id) == 1);
    int64_t normal_uid = 0;
    rc = mf_auth_repo_create(pool, "sec_normal_user", "hash_sec_normal_1234", &normal_uid);
    assert(rc == 0 && normal_uid > 0);
    assert(mf_auth_repo_is_instance_admin(pool, normal_uid) == 0);

    mf_mcp_usecase_result_t mres = {0};
    int64_t                 norm_srv_id = 0;
    mf_mcp_create_cmd_t     norm_cmd = {
        .user_id = normal_uid,
        .name = "norm_srv_sec",
        .transport = "http",
        .url = "https://8.8.8.8/mcp",
        .secret_ref = "MCP_SECRET_GITHUB_TOKEN",
        .timeout_ms = 5000,
    };
    assert(mf_mcp_usecase_create(pool, &norm_cmd, &norm_srv_id, &mres) != 0);
    assert(mres.code == 1004);

    norm_cmd.secret_ref = "JWT_SECRET";
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_create(pool, &norm_cmd, &norm_srv_id, &mres) != 0);
    assert(mres.code == 1004);

    norm_cmd.secret_ref = "";
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_create(pool, &norm_cmd, &norm_srv_id, &mres) == 0);
    assert(mres.code == 0 && norm_srv_id > 0);

    mf_mcp_update_cmd_t norm_upd = {
        .user_id = normal_uid,
        .server_id = norm_srv_id,
        .name = "norm_srv_sec",
        .transport = "http",
        .url = "https://8.8.8.8/mcp",
        .secret_ref = "MCP_SECRET_GITHUB_TOKEN",
        .timeout_ms = 5000,
        .enabled = true,
    };
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_update(pool, &norm_upd, &mres) != 0);
    assert(mres.code == 1004);

    int64_t             admin_srv_id = 0;
    mf_mcp_create_cmd_t admin_cmd = {
        .user_id = user_id,
        .name = "admin_srv_sec",
        .transport = "http",
        .url = "https://8.8.8.8/mcp",
        .secret_ref = "JWT_SECRET",
        .timeout_ms = 5000,
    };
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_create(pool, &admin_cmd, &admin_srv_id, &mres) != 0);
    assert(mres.code == 1002);

    admin_cmd.secret_ref = "MCP_SECRET_../x";
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_create(pool, &admin_cmd, &admin_srv_id, &mres) != 0);
    assert(mres.code == 1002);

    admin_cmd.secret_ref = "MCP_SECRET_GITHUB_TOKEN";
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_create(pool, &admin_cmd, &admin_srv_id, &mres) == 0);
    assert(mres.code == 0 && admin_srv_id > 0);

    mf_mcp_update_cmd_t admin_upd = {
        .user_id = user_id,
        .server_id = admin_srv_id,
        .name = "admin_srv_sec",
        .transport = "http",
        .url = "https://8.8.8.8/mcp",
        .secret_ref = "JWT_SECRET",
        .timeout_ms = 5000,
        .enabled = true,
    };
    memset(&mres, 0, sizeof(mres));
    assert(mf_mcp_usecase_update(pool, &admin_upd, &mres) != 0);
    assert(mres.code == 1002);
}

typedef struct {
    int  listen_fd;
    int  expected_reqs;
    char captured[4][4096];
} mock_mcp_http_srv_t;

static void*
mock_mcp_http_server_thread(void* arg)
{
    mock_mcp_http_srv_t* srv = (mock_mcp_http_srv_t*)arg;
    const char*          body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"ok\":1}}";
    char                 resp[512];
    snprintf(resp,
             sizeof(resp),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             strlen(body),
             body);

    for (int i = 0; i < srv->expected_reqs; i++) {
        int cfd = accept(srv->listen_fd, NULL, NULL);
        if (cfd < 0) {
            break;
        }
        size_t total = 0;
        while (total + 1 < sizeof(srv->captured[i])) {
            ssize_t n = recv(cfd, srv->captured[i] + total, sizeof(srv->captured[i]) - 1 - total, 0);
            if (n <= 0) {
                break;
            }
            total += (size_t)n;
            srv->captured[i][total] = '\0';
            if (strstr(srv->captured[i], "\r\n\r\n") != NULL &&
                strstr(srv->captured[i], "\"method\"") != NULL) {
                break;
            }
        }
        size_t sent = 0;
        size_t rlen = strlen(resp);
        while (sent < rlen) {
            ssize_t w = send(cfd, resp + sent, rlen - sent, 0);
            if (w <= 0) {
                break;
            }
            sent += (size_t)w;
        }
        close(cfd);
    }
    return NULL;
}

static void
test_secret_path_traversal_and_mcp_crlf(void)
{
    printf("[5] config_secret_get traversal, mf_mcp_secret_ref_valid, MCP HTTP CRLF defense...\n");

    char sec_buf[256] = {0};
    assert(config_secret_get("../../etc/passwd", sec_buf, sizeof(sec_buf)) == NULL);
    assert(config_secret_get("foo/bar", sec_buf, sizeof(sec_buf)) == NULL);
    assert(config_secret_get("MINEFOLIO_../../etc/passwd", sec_buf, sizeof(sec_buf)) == NULL);
    assert(config_secret_get("MINEFOLIO_", sec_buf, sizeof(sec_buf)) == NULL);

    assert(mf_mcp_secret_ref_valid("JWT_SECRET") == false);
    assert(mf_mcp_secret_ref_valid("MCP_SECRET_../x") == false);
    assert(mf_mcp_secret_ref_valid("MCP_SECRET_") == false);
    assert(mf_mcp_secret_ref_valid("MCP_SECRET_BAD-NAME") == false);
    assert(mf_mcp_secret_ref_valid(NULL) == false);
    assert(mf_mcp_secret_ref_valid("") == false);
    assert(mf_mcp_secret_ref_valid("MCP_SECRET_GITHUB_TOKEN") == true);
    assert(mf_mcp_secret_ref_valid("MCP_SECRET_a1_B2") == true);

    /* Spin up local loopback HTTP mock to verify assembled wire headers */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(lfd >= 0);
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    assert(listen(lfd, 8) == 0);

    socklen_t alen = sizeof(addr);
    assert(getsockname(lfd, (struct sockaddr*)&addr, &alen) == 0);
    int port = (int)ntohs(addr.sin_port);

    mock_mcp_http_srv_t srv = {.listen_fd = lfd, .expected_reqs = 4};
    pthread_t           th;
    assert(pthread_create(&th, NULL, mock_mcp_http_server_thread, &srv) == 0);

    config_secret_set_test_mode(true);
    config_secret_set_test_override("MINEFOLIO_MCP_ALLOW_LOCAL", "1");
    config_secret_set_test_override("MCP_SECRET_TEST_CRLF", "tok\r\nX-Injected-Auth: evil");
    config_secret_set_test_override("MCP_SECRET_TEST_GOOD", "valid_mcp_token_99");

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/mcp", port);

    /* Req 1: headers_json with CRLF in value and key must be skipped while good header remains */
    {
        mf_mcp_server_t s = {
            .name = "hdr_crlf",
            .transport = MCP_TRANSPORT_HTTP,
            .timeout_ms = 5000,
        };
        snprintf(s.url, sizeof(s.url), "%s", url);
        snprintf(s.headers_json,
                 sizeof(s.headers_json),
                 "{\"X-Good\":\"ok\",\"X-Bad-Val\":\"a\\r\\nX-Injected: evil\",\"X-Bad\\nKey\":\"1\"}");
        mf_mcp_client_t* c = mf_mcp_client_new(&s);
        assert(c != NULL);
        char  err[256] = {0};
        char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
        assert(resp != NULL);
        free(resp);
        mf_mcp_client_free(c);

        assert(strstr(srv.captured[0], "X-Good: ok") != NULL);
        assert(strstr(srv.captured[0], "X-Injected:") == NULL);
        assert(strstr(srv.captured[0], "X-Bad-Val:") == NULL);
        assert(strstr(srv.captured[0], "X-Bad") == NULL);
    }

    /* Req 2: secret_ref token containing CRLF must not be emitted in Authorization header */
    {
        mf_mcp_server_t s = {
            .name = "sec_crlf",
            .transport = MCP_TRANSPORT_HTTP,
            .timeout_ms = 5000,
        };
        snprintf(s.url, sizeof(s.url), "%s", url);
        snprintf(s.secret_ref, sizeof(s.secret_ref), "MCP_SECRET_TEST_CRLF");
        mf_mcp_client_t* c = mf_mcp_client_new(&s);
        assert(c != NULL);
        char  err[256] = {0};
        char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
        assert(resp != NULL);
        free(resp);
        mf_mcp_client_free(c);

        assert(strstr(srv.captured[1], "X-Injected-Auth:") == NULL);
        assert(strstr(srv.captured[1], "Authorization:") == NULL);
    }

    /* Req 3: non-MCP_SECRET_ secret_ref must not be read/sent even if present in config/env */
    {
        mf_mcp_server_t s = {
            .name = "sec_non_allow",
            .transport = MCP_TRANSPORT_HTTP,
            .timeout_ms = 5000,
        };
        snprintf(s.url, sizeof(s.url), "%s", url);
        snprintf(s.secret_ref, sizeof(s.secret_ref), "JWT_SECRET");
        mf_mcp_client_t* c = mf_mcp_client_new(&s);
        assert(c != NULL);
        char  err[256] = {0};
        char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
        assert(resp != NULL);
        free(resp);
        mf_mcp_client_free(c);

        assert(strstr(srv.captured[2], "Authorization:") == NULL);
        assert(strstr(srv.captured[2], "test_jwt_secret_sec_regressions_32b") == NULL);
    }

    /* Req 4: valid MCP_SECRET_* token without CRLF is sent in Authorization header */
    {
        mf_mcp_server_t s = {
            .name = "sec_good",
            .transport = MCP_TRANSPORT_HTTP,
            .timeout_ms = 5000,
        };
        snprintf(s.url, sizeof(s.url), "%s", url);
        snprintf(s.secret_ref, sizeof(s.secret_ref), "MCP_SECRET_TEST_GOOD");
        mf_mcp_client_t* c = mf_mcp_client_new(&s);
        assert(c != NULL);
        char  err[256] = {0};
        char* resp = mf_mcp_client_call_tool(c, "dummy", "{}", err, sizeof(err));
        assert(resp != NULL);
        free(resp);
        mf_mcp_client_free(c);

        assert(strstr(srv.captured[3], "Authorization: Bearer valid_mcp_token_99") != NULL);
    }

    pthread_join(th, NULL);
    close(lfd);
    config_secret_set_test_override("MINEFOLIO_MCP_ALLOW_LOCAL", NULL);
    config_secret_set_test_override("MCP_SECRET_TEST_CRLF", NULL);
    config_secret_set_test_override("MCP_SECRET_TEST_GOOD", NULL);
}

char* receipt_base64_encode(const unsigned char* data, size_t input_length, size_t* output_length);

static void
test_receipt_base64_padding(void)
{
    printf("[6] Receipt base64 RFC 4648 padding vectors...\n");

    static const struct {
        const char* input;
        const char* expected;
    } vectors[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };

    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        size_t out_len = 0;
        char*  enc = receipt_base64_encode(
            (const unsigned char*)vectors[i].input, strlen(vectors[i].input), &out_len);
        assert(enc != NULL);
        assert(out_len == strlen(vectors[i].expected));
        assert(strcmp(enc, vectors[i].expected) == 0);
        free(enc);
    }

    /* Binary input with high bit and trailing zero bytes to ensure proper masking/padding */
    const unsigned char bin[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    size_t              bin_len = 0;
    char*               bin_enc = receipt_base64_encode(bin, sizeof(bin), &bin_len);
    assert(bin_enc != NULL);
    assert(bin_len == 8);
    assert(strcmp(bin_enc, "3q2+7w==") == 0);
    free(bin_enc);
}

static void
test_config_get_str_oob_regression(void)
{
    printf("[7] config_get_str bare key EOF OOB regression...\n");

    const char* cfg_path = "/tmp/test_mf_config_oob.json";
    char        out[64] = {0};

    /* Case 1: file ends immediately after bare key without colon or quotes */
    {
        FILE* f = fopen(cfg_path, "wb");
        assert(f != NULL);
        fputs("{\"bare_key\"", f);
        fclose(f);

        memset(out, 'X', sizeof(out));
        int rc = config_get_str(cfg_path, "bare_key", out, sizeof(out));
        assert(rc == -1);
        assert(out[0] == '\0');
    }

    /* Case 2: file ends right after colon/whitespace without opening quote */
    {
        FILE* f = fopen(cfg_path, "wb");
        assert(f != NULL);
        fputs("{\"bare_key\":   ", f);
        fclose(f);

        memset(out, 'X', sizeof(out));
        int rc = config_get_str(cfg_path, "bare_key", out, sizeof(out));
        assert(rc == -1);
        assert(out[0] == '\0');
    }

    /* Case 2b: 4095-byte file ending right at buf[4095] NUL after bare key (strict ASan OOB check) */
    {
        FILE* f = fopen(cfg_path, "wb");
        assert(f != NULL);
        for (int i = 0; i < 4084; i++) {
            fputc(' ', f);
        }
        fputs("{\"bare_key\"", f);
        fclose(f);

        memset(out, 'X', sizeof(out));
        int rc = config_get_str(cfg_path, "bare_key", out, sizeof(out));
        assert(rc == -1);
        assert(out[0] == '\0');
    }

    /* Case 3: valid quoted value after a bare key prefix still parses */
    {
        FILE* f = fopen(cfg_path, "wb");
        assert(f != NULL);
        fputs("{\"bare_key\": \"ok_val\"}", f);
        fclose(f);

        memset(out, 0, sizeof(out));
        int rc = config_get_str(cfg_path, "bare_key", out, sizeof(out));
        assert(rc == 0);
        assert(strcmp(out, "ok_val") == 0);
    }

    unlink(cfg_path);
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
    test_secret_path_traversal_and_mcp_crlf();
    test_receipt_base64_padding();
    test_config_get_str_oob_regression();

    cleanup_db_files();
    printf("=== All Security & Audit Regression Tests Passed ===\n");
    return 0;
}
