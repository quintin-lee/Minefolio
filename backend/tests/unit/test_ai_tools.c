#include "services/ai/tools/registry.h"
#include "services/ai/tools/dispatcher.h"
#include "services/ai/tools/validation.h"
#include "services/ai/tools/context.h"
#include "services/ai/tools/web_search_tool.h"
#include "services/ai/policy/confirmation.h"
#include "domain/ai/rules.h"
#include "application/skill/usecases.h"
#include "infrastructure/repositories/asset_repo_impl.h"
#include "common/db.h"
#include "common/config.h"
#include "csilk/csilk.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t expected_all;
    size_t expected_full;
    size_t expected_ro;
} registry_race_expect_t;

static void*
registry_reader_worker(void* arg)
{
    const registry_race_expect_t* exp = (const registry_race_expect_t*)arg;
    for (int iter = 0; iter < 2000; iter++) {
        size_t           all_cnt = 0;
        size_t           full_cnt = 0;
        size_t           ro_cnt = 0;
        const ai_tool_t**      all = ai_tool_list_all(&all_cnt);
        const csilk_ai_tool_t* full = ai_tool_get_csilk_definitions(&full_cnt);
        const csilk_ai_tool_t* ro = ai_tool_get_readonly_csilk_definitions(&ro_cnt);

        assert(all != NULL && all_cnt == exp->expected_all);
        assert(full != NULL && full_cnt == exp->expected_full);
        assert(ro != NULL && ro_cnt == exp->expected_ro);
        assert(ro_cnt <= full_cnt && full_cnt <= all_cnt);

        for (size_t i = 0; i < full_cnt; i++) {
            assert(full[i].function.name != NULL && full[i].function.name[0] != '\0');
            const ai_tool_t* meta = ai_tool_find(full[i].function.name);
            assert(meta != NULL && !meta->internal_only);
        }
        for (size_t i = 0; i < ro_cnt; i++) {
            assert(ro[i].function.name != NULL && ro[i].function.name[0] != '\0');
            const ai_tool_t* meta = ai_tool_find(ro[i].function.name);
            assert(meta != NULL && !meta->internal_only && !meta->is_mutation);
        }
    }
    return NULL;
}

static void
test_registry_thread_safety(void)
{
    printf("[Test Registry Concurrency] Spawning 8 reader threads...\n");
    registry_race_expect_t exp = {0};
    assert(ai_tool_list_all(&exp.expected_all) != NULL && exp.expected_all > 0);
    assert(ai_tool_get_csilk_definitions(&exp.expected_full) != NULL && exp.expected_full > 0);
    assert(ai_tool_get_readonly_csilk_definitions(&exp.expected_ro) != NULL && exp.expected_ro > 0);

    pthread_t tids[8];
    for (int i = 0; i < 8; i++) {
        int rc = pthread_create(&tids[i], NULL, registry_reader_worker, &exp);
        assert(rc == 0);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(tids[i], NULL);
    }
    printf("[Test Registry Concurrency] PASS: 8 threads x 2000 iterations race-free\n");
}

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

static void
test_ai_ssrf_url_validation(void)
{
    printf("[Test SSRF] Testing mf_ai_rule_validate_url...\n");
    /* Null & empty */
    assert(!mf_ai_rule_validate_url(NULL, 4096));
    assert(!mf_ai_rule_validate_url("", 4096));

    /* Scheme checks */
    assert(mf_ai_rule_validate_url("https://blogs.nvidia.cn/blog/test", 4096));
    assert(mf_ai_rule_validate_url("http://example.com/path?a=1&b=2", 4096));
    assert(!mf_ai_rule_validate_url("ftp://example.com", 4096));
    assert(!mf_ai_rule_validate_url("file:///etc/passwd", 4096));
    assert(!mf_ai_rule_validate_url("javascript:alert(1)", 4096));

    /* Loopback & Localhost */
    assert(!mf_ai_rule_validate_url("http://localhost/admin", 4096));
    assert(!mf_ai_rule_validate_url("http://localhost:8080/api", 4096));
    assert(!mf_ai_rule_validate_url("http://127.0.0.1:8080/api", 4096));
    assert(!mf_ai_rule_validate_url("http://127.0.0.2:3000", 4096));
    assert(!mf_ai_rule_validate_url("http://0.0.0.0", 4096));
    assert(!mf_ai_rule_validate_url("http://0:8080", 4096));
    assert(!mf_ai_rule_validate_url("http://017700000001:8080/", 4096));
    assert(!mf_ai_rule_validate_url("http://2130706433:8080/", 4096));
    assert(!mf_ai_rule_validate_url("http://0x7f000001:8080/", 4096));
    assert(!mf_ai_rule_validate_url("http://[::1]:8080", 4096));
    assert(!mf_ai_rule_validate_url("http://::1", 4096));
    assert(!mf_ai_rule_validate_url("http://[::ffff:127.0.0.1]:8080", 4096));

    /* Private IPv4 subnets */
    assert(!mf_ai_rule_validate_url("http://10.0.0.1/status", 4096));
    assert(!mf_ai_rule_validate_url("http://10.255.255.255", 4096));
    assert(!mf_ai_rule_validate_url("http://192.168.1.100", 4096));
    assert(!mf_ai_rule_validate_url("http://192.168.0.1:80", 4096));
    assert(!mf_ai_rule_validate_url("http://172.16.5.1", 4096));
    assert(!mf_ai_rule_validate_url("http://172.31.255.255", 4096));
    assert(mf_ai_rule_validate_url("http://172.15.0.1", 4096));  /* Public 172.15 */
    assert(mf_ai_rule_validate_url("http://172.32.0.1", 4096));  /* Public 172.32 */
    assert(!mf_ai_rule_validate_url("http://169.254.1.1", 4096)); /* Link-local */
    /* Private TLD domains (SEC-03) */
    assert(!mf_ai_rule_validate_url("http://service.local/api", 4096));
    assert(!mf_ai_rule_validate_url("http://meta.internal/latest", 4096));
    assert(!mf_ai_rule_validate_url("http://nas.lan/share", 4096));
    assert(!mf_ai_rule_validate_url("http://gateway.home.arpa/status", 4096));

    /* Length limits */
    char long_url[5000];
    memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "https://example.com/", 20);
    long_url[4000] = '\0';
    assert(mf_ai_rule_validate_url(long_url, 4096)); /* 4000 <= 4096 */

    memset(long_url + 4000, 'b', 97);
    long_url[4097] = '\0';
    assert(!mf_ai_rule_validate_url(long_url, 4096)); /* 4097 > 4096 */
    printf("[Test SSRF] PASS: mf_ai_rule_validate_url verified\n");
}

/** iLink query+confirm 能力档白名单：只能取到 propose_*，绝不包含 internal_only 的 confirm_*。 */
static void
test_im_bookkeeping_tool_subset(void)
{
    static const char* const names[] = {
        "propose_daily_expense", "confirm_proposed_expense", "confirm_proposed_transfer",
        "no_such_tool"};
    csilk_ai_tool_t subset[8] = {0};
    size_t          n = ai_tool_get_csilk_definitions_subset(names, 4, subset, 8);
    assert(n == 1);
    assert(strcmp(subset[0].function.name, "propose_daily_expense") == 0);

    size_t                 ro_count = 0;
    const csilk_ai_tool_t* ro = ai_tool_get_readonly_csilk_definitions(&ro_count);
    assert(ro != NULL);
    for (size_t i = 0; i < ro_count; i++) {
        assert(strcmp(ro[i].function.name, "propose_daily_expense") != 0);
        assert(strcmp(ro[i].function.name, "confirm_proposed_expense") != 0);
    }
    printf("[Test IM Bookkeeping Whitelist] PASS: propose only, no confirm tools\n");
}

static char*
make_confirm_json(const char* draft_args_json,
                  const char* token,
                  const char* tamper_key,
                  const char* tamper_val)
{
    csilk_json_t* parsed = csilk_json_parse(draft_args_json);
    assert(parsed != NULL);
    csilk_json_t* mut = csilk_json_copy(parsed);
    csilk_json_free(parsed);
    assert(mut != NULL);
    if (tamper_key && tamper_val) {
        csilk_json_t* next = csilk_json_object();
        size_t        n = csilk_json_object_size(mut);
        for (size_t i = 0; i < n; i++) {
            const char*         k = csilk_json_object_key(mut, i);
            const csilk_json_t* v = csilk_json_object_val(mut, i);
            if (strcmp(k, tamper_key) == 0) {
                continue;
            }
            if (csilk_json_is_string(v)) {
                csilk_json_add_string(next, k, csilk_json_string_value(v));
            } else if (csilk_json_is_number(v)) {
                csilk_json_add_number(next, k, csilk_json_number_value(v));
            } else if (csilk_json_is_bool(v)) {
                csilk_json_add_bool(next, k, csilk_json_bool_value(v));
            }
        }
        csilk_json_add_string(next, tamper_key, tamper_val);
        csilk_json_free(mut);
        mut = next;
    }
    csilk_json_add_string(mut, "draft_token", token);
    char* out = csilk_json_serialize(mut, NULL);
    csilk_json_free(mut);
    return out;
}

int
main(void)
{
    printf("=== Running Integration Unit Test: AI Tool System ===\n");
    test_ai_ssrf_url_validation();
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

    /* 0. Verify confirm_* tools are NOT exposed to LLM (SEC-02) */
    printf("[Test 0] Verifying confirm tools are internal-only (not in LLM definitions)...\n");
    {
        static const char* const k_confirm_tools[] = {
            "confirm_proposed_transfer",
            "confirm_proposed_expense",
            "confirm_proposed_category",
            "confirm_proposed_tag",
            "confirm_proposed_savings_goal",
            "confirm_proposed_transaction",
        };
        static const char* const k_propose_tools[] = {
            "propose_daily_expense",
            "propose_transfer",
            "propose_batch_actions",
            "propose_create_category",
            "propose_create_tag",
            "propose_savings_goal",
            "propose_transaction",
        };
        size_t csilk_count = 0;
        const csilk_ai_tool_t* defs = ai_tool_get_csilk_definitions(&csilk_count);
        assert(defs != NULL && csilk_count > 0);
        for (size_t i = 0; i < csilk_count; i++) {
            for (size_t k = 0; k < sizeof(k_confirm_tools) / sizeof(k_confirm_tools[0]); k++) {
                assert(strcmp(defs[i].function.name, k_confirm_tools[k]) != 0);
            }
        }

        size_t ro_cnt = 0;
        const csilk_ai_tool_t* ro_defs = ai_tool_get_readonly_csilk_definitions(&ro_cnt);
        assert(ro_defs != NULL && ro_cnt > 0);
        for (size_t i = 0; i < ro_cnt; i++) {
            for (size_t k = 0; k < sizeof(k_confirm_tools) / sizeof(k_confirm_tools[0]); k++) {
                assert(strcmp(ro_defs[i].function.name, k_confirm_tools[k]) != 0);
            }
            for (size_t k = 0; k < sizeof(k_propose_tools) / sizeof(k_propose_tools[0]); k++) {
                assert(strcmp(ro_defs[i].function.name, k_propose_tools[k]) != 0);
            }
        }

        /* 但是通过 ai_tool_find 内部仍可查到供人类交互确认调用 */
        for (size_t k = 0; k < sizeof(k_confirm_tools) / sizeof(k_confirm_tools[0]); k++) {
            const ai_tool_t* ct = ai_tool_find(k_confirm_tools[k]);
            assert(ct != NULL && ct->internal_only && ct->is_mutation && ct->requires_id);
        }
        for (size_t k = 0; k < sizeof(k_propose_tools) / sizeof(k_propose_tools[0]); k++) {
            const ai_tool_t* pt = ai_tool_find(k_propose_tools[k]);
            assert(pt != NULL && !pt->internal_only && pt->is_mutation && pt->requires_id);
        }

        /* action_type -> confirm_* 动态路由 */
        assert(strcmp(ai_tool_find_confirm_tool_by_action("daily_expense"),
                      "confirm_proposed_expense") == 0);
        assert(strcmp(ai_tool_find_confirm_tool_by_action("transfer"),
                      "confirm_proposed_transfer") == 0);
        assert(strcmp(ai_tool_find_confirm_tool_by_action("create_category"),
                      "confirm_proposed_category") == 0);
        assert(strcmp(ai_tool_find_confirm_tool_by_action("create_tag"),
                      "confirm_proposed_tag") == 0);
        assert(strcmp(ai_tool_find_confirm_tool_by_action("savings_goal"),
                      "confirm_proposed_savings_goal") == 0);
        assert(strcmp(ai_tool_find_confirm_tool_by_action("transaction"),
                      "confirm_proposed_transaction") == 0);
        assert(ai_tool_find_confirm_tool_by_action("non_existent_action") == NULL);
    }

    test_im_bookkeeping_tool_subset();
    test_registry_thread_safety();

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
            assert(strstr(exec_fail, "error") != NULL && (strstr(exec_fail, "篡改") != NULL || strstr(exec_fail, "不一致") != NULL || strstr(exec_fail, "invalid") != NULL));
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
            assert(strstr(exec_replay, "error") != NULL && (strstr(exec_replay, "不匹配") != NULL || strstr(exec_replay, "越权") != NULL || strstr(exec_replay, "invalid") != NULL));
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
            csilk_json_t* asset = asset_get(pool, user_id, 0, 101);
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
        csilk_json_t* a101 = asset_get(pool, user_id, 0, 101);
        csilk_json_t* a102 = asset_get(pool, user_id, 0, 102);
        assert(a101 != NULL && csilk_json_array_size(a101) > 0);
        assert(a102 != NULL && csilk_json_array_size(a102) > 0);
        assert(db_get_num(csilk_json_array_get(a101, 0), "current_value") == 4550.0);
        assert(db_get_num(csilk_json_array_get(a102, 0), "current_value") == 800.0);
        csilk_json_free(a101);
        csilk_json_free(a102);

        csilk_json_free(tf_obj);
        free(prop_tf);
    }

    /* 8. Web Search & Fetch Webpage Tools */
    printf("[Test 8] Testing AI Web Search & Fetch Webpage Tools...\n");
    {
        /* Check registry finding */
        const ai_tool_t* t_search = ai_tool_find("web_search");
        assert(t_search != NULL);
        assert(t_search->permission == AI_PERM_READ);
        assert(t_search->risk == AI_RISK_LOW);
        assert(!t_search->is_mutation);

        const ai_tool_t* t_fetch = ai_tool_find("fetch_webpage");
        assert(t_fetch != NULL);
        assert(t_fetch->permission == AI_PERM_READ);
        assert(t_fetch->risk == AI_RISK_LOW);
        assert(!t_fetch->is_mutation);

        /* Schema validation: missing required fields */
        char* bad_search = ai_tool_dispatch(ctx, "web_search", "{}");
        assert(bad_search != NULL);
        assert(strstr(bad_search, "Missing required field") != NULL || strstr(bad_search, "query") != NULL);
        free(bad_search);

        char* bad_fetch = ai_tool_dispatch(ctx, "fetch_webpage", "{}");
        assert(bad_fetch != NULL);
        assert(strstr(bad_fetch, "Missing required field") != NULL || strstr(bad_fetch, "url") != NULL);
        free(bad_fetch);

        /* SSRF rejection on fetch_webpage */
        char* ssrf_local = ai_tool_dispatch(ctx, "fetch_webpage", "{\"url\":\"http://127.0.0.1:8080/api/users\"}");
        assert(ssrf_local != NULL);
        assert(strstr(ssrf_local, "forbidden_url") != NULL);
        free(ssrf_local);

        char* ssrf_private = ai_tool_dispatch(ctx, "fetch_webpage", "{\"url\":\"http://192.168.1.1/admin\"}");
        assert(ssrf_private != NULL);
        assert(strstr(ssrf_private, "forbidden_url") != NULL);
        free(ssrf_private);

        /* 4. Mock SERP Parser & Bing base64 URL unpacker */
        char* unpacked_direct = mf_bing_unpack_url("https://example.com/direct");
        assert(strcmp(unpacked_direct, "https://example.com/direct") == 0);
        free(unpacked_direct);

        char* unpacked_tracking = mf_bing_unpack_url("https://www.bing.com/ck/a?!&&p=abc&u=a1aHR0cHM6Ly9ibG9ncy5udmlkaWEuY24vYmxvZy90ZXN0&ntb=1");
        assert(strcmp(unpacked_tracking, "https://blogs.nvidia.cn/blog/test") == 0);
        free(unpacked_tracking);

        const char* mock_bing_html =
            "<ol id=\"b_results\">"
            "<li class=\"b_algo\">"
            "  <h2><a href=\"https://example.com/page1\"><strong>Title</strong> &amp; News</a></h2>"
            "  <div class=\"b_caption\"><p class=\"b_lineclamp2\">Snippet &quot;preview&quot; here.</p></div>"
            "</li>"
            "</ol>";
        csilk_json_t* parsed_results = mf_bing_html_parse(mock_bing_html, 5);
        assert(parsed_results != NULL);
        assert(csilk_json_array_size(parsed_results) == 1);
        const csilk_json_t* first_res = csilk_json_array_get(parsed_results, 0);
        assert(strcmp(csilk_json_get_string(first_res, "title"), "Title & News") == 0);
        assert(strcmp(csilk_json_get_string(first_res, "url"), "https://example.com/page1") == 0);
        assert(strcmp(csilk_json_get_string(first_res, "snippet"), "Snippet \"preview\" here.") == 0);
        csilk_json_free(parsed_results);

        /* 5. Bing redirect URL query parameter normalization */
        char bing_red[512] = "https://www.bing.com/?q=apple&mkt=zh-CN";
        mf_bing_normalize_redirect_url(bing_red, sizeof(bing_red));
        assert(strcmp(bing_red, "https://www.bing.com/search?q=apple&mkt=zh-CN") == 0);

        /* 6. UTF-8 Character Boundary Safe Truncation */
        char utf8_text[64] = "你好世界Minefolio";
        size_t cut_len = mf_utf8_truncate(utf8_text, 5); /* 5 falls inside '好' (bytes 3-5) */
        assert(cut_len == 3);
        assert(strcmp(utf8_text, "你") == 0);

        if (getenv("MINEFOLIO_TEST_LIVE_WEB")) {
            printf("\n--- Running Live Web Search & Fetch Verification ---\n");
            char* live_s = ai_tool_dispatch(ctx, "web_search", "{\"query\":\"美联储 利率\",\"num_results\":2}");
            printf("Live search response:\n%s\n", live_s);
            assert(live_s != NULL && strstr(live_s, "results") != NULL);
            free(live_s);

            char* live_f = ai_tool_dispatch(ctx, "fetch_webpage", "{\"url\":\"https://example.com\",\"max_chars\":500}");
            printf("Live fetch response:\n%s\n", live_f);
            assert(live_f != NULL && strstr(live_f, "Example Domain") != NULL);
            free(live_f);
        }

        printf("[Test 8] PASS: Web search & deep reading tools verified\n");
    }

    /* 9. Propose Batch Actions Tool & Protocol */
    {
        printf("[Test 9] Testing Propose Batch Actions Tool & Protocol...\n");

        /* 1. Tool registration & metadata */
        const ai_tool_t* t_batch = ai_tool_find("propose_batch_actions");
        assert(t_batch != NULL);
        assert(t_batch->permission == AI_PERM_WRITE);
        assert(t_batch->risk == AI_RISK_MEDIUM);
        assert(t_batch->is_mutation == true);
        assert(t_batch->requires_id == true);

        /* 2. Empty actions rejected */
        char* r_empty = ai_tool_dispatch(ctx, "propose_batch_actions", "{\"actions\":[]}");
        assert(r_empty != NULL);
        assert(strstr(r_empty, "error") != NULL);
        free(r_empty);

        /* 3. Action count > 20 rejected */
        csilk_json_t* big_req = csilk_json_object();
        csilk_json_t* big_actions = csilk_json_array();
        for (int i = 0; i < 21; i++) {
            csilk_json_t* a = csilk_json_object();
            csilk_json_add_string(a, "action_type", "daily_expense");
            csilk_json_add_number(a, "amount", 10.0);
            csilk_json_array_append(big_actions, a);
        }
        csilk_json_add_array(big_req, "actions", big_actions);
        size_t big_len = 0;
        char* big_json = csilk_json_serialize(big_req, &big_len);
        csilk_json_free(big_req);
        char* r_large = ai_tool_dispatch(ctx, "propose_batch_actions", big_json);
        free(big_json);
        assert(r_large != NULL);
        assert(strstr(r_large, "error") != NULL);
        free(r_large);

        /* 4. Action with invalid amount <= 0 rejected */
        char* r_bad_amt = ai_tool_dispatch(ctx, "propose_batch_actions",
            "{\"actions\":[{\"action_type\":\"daily_expense\",\"amount\":0.0}]}");
        assert(r_bad_amt != NULL);
        assert(strstr(r_bad_amt, "error") != NULL);
        free(r_bad_amt);

        char* r_neg_amt = ai_tool_dispatch(ctx, "propose_batch_actions",
            "{\"actions\":[{\"action_type\":\"daily_expense\",\"amount\":-15.0}]}");
        assert(r_neg_amt != NULL);
        assert(strstr(r_neg_amt, "error") != NULL);
        free(r_neg_amt);

        /* 5. Unknown action_type rejected */
        char* r_bad_type = ai_tool_dispatch(ctx, "propose_batch_actions",
            "{\"actions\":[{\"action_type\":\"crypto_trade\",\"amount\":100.0}]}");
        assert(r_bad_type != NULL);
        assert(strstr(r_bad_type, "error") != NULL);
        free(r_bad_type);

        /* 6. Valid batch: 1 expense + 1 transfer */
        const char* valid_payload =
            "{\"actions\":["
            "{\"action_type\":\"daily_expense\",\"amount\":45.0,\"type\":\"expense\",\"category_name\":\"餐饮美食\",\"asset_name\":\"招商储蓄卡\",\"note\":\"午餐\"},"
            "{\"action_type\":\"transfer\",\"amount\":200.0,\"from_asset_name\":\"招商储蓄卡\",\"to_asset_name\":\"微信零钱\",\"note\":\"零花钱\"}"
            "]}";
        char* r_ok = ai_tool_dispatch(ctx, "propose_batch_actions", valid_payload);
        assert(r_ok != NULL);
        assert(strstr(r_ok, "propose_success") != NULL);
        assert(strstr(r_ok, "requires_confirmation") != NULL);
        assert(strstr(r_ok, "mf_batch_v1.") != NULL);
        assert(strstr(r_ok, "summary") != NULL);

        csilk_json_t* res_obj = csilk_json_parse(r_ok);
        assert(res_obj != NULL);
        assert(csilk_json_get_bool(res_obj, "propose_success") == true);
        assert(csilk_json_get_bool(res_obj, "requires_confirmation") == true);
        assert((int64_t)db_get_num(res_obj, "count") == 2);

        const char* draft_token = csilk_json_get_string(res_obj, "draft_token");
        assert(draft_token != NULL && strncmp(draft_token, "mf_batch_v1.", 12) == 0);

        const char* summary = csilk_json_get_string(res_obj, "summary");
        assert(summary != NULL);
        assert(strstr(summary, "45.00") != NULL);
        assert(strstr(summary, "200.00") != NULL);

        csilk_json_t* act_arr = csilk_json_get(res_obj, "actions");
        assert(act_arr != NULL && csilk_json_is_array(act_arr));
        assert(csilk_json_array_size(act_arr) == 2);

        /* 7. Verify draft_token validates successfully with ai_confirmation_verify_and_consume_batch */
        ai_confirmation_status_t st = ai_confirmation_verify_and_consume_batch(
            ctx->user_id, ctx->session_id, act_arr, draft_token);
        assert(st == AI_CONFIRM_OK);

        csilk_json_free(res_obj);
        free(r_ok);

        printf("[Test 9] PASS: Propose batch actions tool & HMAC draft protocol verified\n");
    }

    /* Test 10: Agent Skills Discovery and Invocation Tools (use_skill, list_skills) */
    {
        printf("[Test 10] Testing Agent Skills tools...\n");
        const ai_tool_t* t_use = ai_tool_find("use_skill");
        assert(t_use != NULL);
        assert(t_use->permission == AI_PERM_READ);
        assert(!t_use->is_mutation);

        const ai_tool_t* t_list = ai_tool_find("list_skills");
        assert(t_list != NULL);

        /* 1. list_skills */
        char* r_list = ai_tool_dispatch(ctx, "list_skills", "{}");
        assert(r_list != NULL);
        assert(strstr(r_list, "skills") != NULL);
        free(r_list);

        /* 2. use_skill missing parameter */
        char* r_miss = ai_tool_dispatch(ctx, "use_skill", "{}");
        assert(r_miss != NULL);
        assert(strstr(r_miss, "error") != NULL);
        free(r_miss);

        /* 3. use_skill not found */
        char* r_notfound = ai_tool_dispatch(ctx, "use_skill", "{\"skill_name\":\"non-existent-skill\"}");
        assert(r_notfound != NULL);
        assert(strstr(r_notfound, "not found") != NULL);
        free(r_notfound);

        /* 4. Seed defaults and invoke L2 skill via use_skill */
        assert(skill_usecase_seed_defaults(ctx->pool, ctx->user_id) == 0);
        char* r_l2 = ai_tool_dispatch(
            ctx, "use_skill", "{\"skill_name\":\"financial-health-auditor\"}");
        assert(r_l2 != NULL);
        assert(strstr(r_l2, "\"status\":\"success\"") != NULL);
        assert(strstr(r_l2, "\"mode\":\"l2\"") != NULL);
        assert(strstr(r_l2, "\"skill_name\":\"financial-health-auditor\"") != NULL);
        assert(strstr(r_l2, "\"instructions\":") != NULL);
        free(r_l2);

        /* 5. L3 skill execution with exec_args and newline-separated exec_env */
        mf_skill_create_dto_t l3_dto = {0};
        strncpy(l3_dto.name, "env-echo-runner", sizeof(l3_dto.name) - 1);
        strncpy(l3_dto.description, "Test L3 args and env forwarding", sizeof(l3_dto.description) - 1);
        strncpy(l3_dto.category, "finance", sizeof(l3_dto.category) - 1);
        l3_dto.enabled = 1;
        l3_dto.instructions_l2 = "# L3 Runner";
        l3_dto.backend = MF_SKILL_BACKEND_LOCAL;
        strncpy(l3_dto.exec_cfg.root, "/usr/bin", sizeof(l3_dto.exec_cfg.root) - 1);
        strncpy(l3_dto.exec_cfg.path, "env", sizeof(l3_dto.exec_cfg.path) - 1);
        strncpy(l3_dto.exec_cfg.env,
                "MF_SKILL_ENV_1=alpha_ok\nMF_SKILL_ENV_2=beta_ok",
                sizeof(l3_dto.exec_cfg.env) - 1);
        int64_t l3_id = 0;
        assert(skill_usecase_create(ctx->pool, ctx->user_id, &l3_dto, &l3_id) == 0);
        assert(l3_id > 0);

        char* r_l3 = ai_tool_dispatch(
            ctx, "use_skill", "{\"skill_name\":\"env-echo-runner\"}");
        assert(r_l3 != NULL);
        assert(strstr(r_l3, "\"mode\":\"l3\"") != NULL);
        assert(strstr(r_l3, "MF_SKILL_ENV_1=alpha_ok") != NULL);
        assert(strstr(r_l3, "MF_SKILL_ENV_2=beta_ok") != NULL);
        free(r_l3);

        printf("[Test 10] PASS: Agent skills tools (use_skill, list_skills, L2/L3) verified\n");
    }

    /* Test 11: get_market_pulse numeric days & ledger_id parsing */
    {
        char uid_s[32];
        snprintf(uid_s, sizeof(uid_s), "%lld", (long long)ctx->user_id);
        assert(csilk_db_exec_param(
                   ctx->pool,
                   "INSERT INTO ai_news_reports (user_id, ledger_id, report_date, title, analysis, status) VALUES "
                   "(?, 20, '2026-10-01', 'T1', 'A1', 'done'), "
                   "(?, 20, '2026-10-02', 'T2', 'A2', 'done'), "
                   "(?, 20, '2026-10-03', 'T3', 'A3', 'done'), "
                   "(?, 99, '2026-10-04', 'T4', 'OtherLedger', 'done')",
                   (const char*[]){uid_s, uid_s, uid_s, uid_s, NULL}) == 0);
        char* pulse = ai_tool_dispatch(ctx, "get_market_pulse", "{\"days\":3,\"ledger_id\":20}");
        assert(pulse != NULL);
        csilk_json_t* parr = csilk_json_parse(pulse);
        assert(parr && csilk_json_is_array(parr) && csilk_json_array_size(parr) == 3);
        assert(strstr(pulse, "OtherLedger") == NULL);
        csilk_json_free(parr);
        free(pulse);
        printf("[Test 11] PASS: get_market_pulse numeric days & ledger_id verified\n");
    }

    /* Test 12: Schema Enum Validation & Read-Only Context Tools */
    printf("[Test 12] Testing Enum Validation & Read-Only Context Tools...\n");
    {
        /* 12a. Enum validation rejections */
        char* bad_cat_enum = ai_tool_dispatch(ctx, "get_categories", "{\"type\":\"bogus\"}");
        assert(bad_cat_enum != NULL && strstr(bad_cat_enum, "Invalid enum value") != NULL);
        free(bad_cat_enum);

        char* bad_tx_enum = ai_tool_dispatch(
            ctx, "propose_transaction", "{\"transaction_type\":\"hack\",\"amount\":10}");
        assert(bad_tx_enum != NULL && strstr(bad_tx_enum, "Invalid enum value") != NULL);
        free(bad_tx_enum);

        /* 12b. forbid_internal blocks confirm_proposed_category */
        ai_tool_context_t* llm_ctx = ai_tool_context_create(pool, user_id, 1, "llm_trace");
        llm_ctx->forbid_internal = true;
        char* blocked_confirm = ai_tool_dispatch(
            llm_ctx, "confirm_proposed_category", "{\"name\":\"X\",\"draft_token\":\"tok\"}");
        assert(blocked_confirm != NULL && strstr(blocked_confirm, "internal_only_tool") != NULL);
        free(blocked_confirm);
        ai_tool_context_free(llm_ctx);

        /* 12c. get_categories */
        char* cats_all = ai_tool_dispatch(ctx, "get_categories", "{}");
        assert(cats_all != NULL && strstr(cats_all, "\"categories\"") != NULL);
        assert(strstr(cats_all, "餐饮美食") != NULL);
        free(cats_all);

        char* cats_exp = ai_tool_dispatch(ctx, "get_categories", "{\"type\":\"expense\"}");
        assert(cats_exp != NULL && strstr(cats_exp, "餐饮美食") != NULL);
        assert(strstr(cats_exp, "储蓄账户") == NULL);
        free(cats_exp);

        /* 12d. get_ledgers & get_ledger_detail (verify invite_code & user_id stripped) */
        assert(csilk_db_exec(pool,
                             "UPDATE ledgers SET invite_code='SECRET_INVITE_CODE' WHERE owner_id=1") ==
               0);
        char* ledgers_res = ai_tool_dispatch(ctx, "get_ledgers", "{}");
        assert(ledgers_res != NULL && strstr(ledgers_res, "\"ledgers\"") != NULL);
        assert(strstr(ledgers_res, "SECRET_INVITE_CODE") == NULL);
        free(ledgers_res);

        char* ledger_detail_res = ai_tool_dispatch(ctx, "get_ledger_detail", "{}");
        assert(ledger_detail_res != NULL && strstr(ledger_detail_res, "\"ledger\"") != NULL);
        assert(strstr(ledger_detail_res, "\"members\"") != NULL);
        assert(strstr(ledger_detail_res, "tester") != NULL);
        assert(strstr(ledger_detail_res, "SECRET_INVITE_CODE") == NULL);
        assert(strstr(ledger_detail_res, "\"user_id\"") == NULL);
        free(ledger_detail_res);

        /* 12e. get_accounts */
        char* accounts_res = ai_tool_dispatch(ctx, "get_accounts", "{}");
        assert(accounts_res != NULL && strstr(accounts_res, "\"accounts\"") != NULL);
        free(accounts_res);

        /* 12f. get_expense_by_tag & get_budget_status */
        char* tag_exp_res = ai_tool_dispatch(ctx, "get_expense_by_tag", "{}");
        assert(tag_exp_res != NULL && strstr(tag_exp_res, "\"tags\"") != NULL);
        assert(strstr(tag_exp_res, "\"total_expense\"") != NULL);
        free(tag_exp_res);

        char* budget_res =
            ai_tool_dispatch(ctx, "get_budget_status", "{\"monthly_budget_limit\":2000}");
        assert(budget_res != NULL && strstr(budget_res, "\"spent_amount\"") != NULL);
        assert(strstr(budget_res, "\"total_budget_amount\":2000") != NULL);
        assert(strstr(budget_res, "\"overall_risk\"") != NULL);
        free(budget_res);

        /* 12g. get_dca_plans & get_cashflow_schedules */
        char* dca_res = ai_tool_dispatch(ctx, "get_dca_plans", "{}");
        assert(dca_res != NULL && strstr(dca_res, "\"plans\"") != NULL);
        free(dca_res);

        char* cf_res = ai_tool_dispatch(ctx, "get_cashflow_schedules", "{}");
        assert(cf_res != NULL && strstr(cf_res, "\"schedules\"") != NULL);
        free(cf_res);

        printf("[Test 12] PASS: Enum validation & read-only context tools verified\n");
    }

    /* Test 13: Proposal Write Tools Lifecycle (Category, Tag, Savings Goal, Transaction) */
    printf("[Test 13] Testing Proposal Write Tools Lifecycle...\n");
    {
        /* 13a. propose_create_category -> tamper -> confirm -> replay */
        char* prop_cat = ai_tool_dispatch(
            ctx,
            "propose_create_category",
            "{\"name\":\"宠物消费\",\"type\":\"expense\",\"icon\":\"ph:cat\"}");
        assert(prop_cat != NULL && strstr(prop_cat, "propose_success") != NULL);
        csilk_json_t* cat_obj = csilk_json_parse(prop_cat);
        assert(cat_obj != NULL);
        const char* cat_tok = csilk_json_get_string(cat_obj, "draft_token");
        const char* cat_args_json = csilk_json_get_string(cat_obj, "draft_args_json");
        assert(cat_tok && cat_tok[0] && cat_args_json && cat_args_json[0]);

        char* tamp_cat_str = make_confirm_json(cat_args_json, cat_tok, "name", "篡改分类");
        char* cat_tamper_res = ai_tool_dispatch(ctx, "confirm_proposed_category", tamp_cat_str);
        free(tamp_cat_str);
        assert(cat_tamper_res != NULL && strstr(cat_tamper_res, "\"error\"") != NULL);
        free(cat_tamper_res);

        char* ok_cat_str = make_confirm_json(cat_args_json, cat_tok, NULL, NULL);
        char* cat_ok_res = ai_tool_dispatch(ctx, "confirm_proposed_category", ok_cat_str);
        assert(cat_ok_res != NULL && strstr(cat_ok_res, "\"success\":true") != NULL);
        free(cat_ok_res);

        char* cat_replay_res = ai_tool_dispatch(ctx, "confirm_proposed_category", ok_cat_str);
        free(ok_cat_str);
        assert(cat_replay_res != NULL && strstr(cat_replay_res, "\"error\"") != NULL);
        free(cat_replay_res);

        char* cats_after = ai_tool_dispatch(ctx, "get_categories", "{\"type\":\"expense\"}");
        assert(cats_after != NULL && strstr(cats_after, "宠物消费") != NULL);
        free(cats_after);
        csilk_json_free(cat_obj);
        free(prop_cat);

        /* 13b. propose_create_tag -> confirm -> get_tags */
        char* prop_tag = ai_tool_dispatch(
            ctx, "propose_create_tag", "{\"name\":\"出差报销\",\"color\":\"#10b981\"}");
        assert(prop_tag != NULL && strstr(prop_tag, "propose_success") != NULL);
        csilk_json_t* tag_obj = csilk_json_parse(prop_tag);
        const char*   tag_tok = csilk_json_get_string(tag_obj, "draft_token");
        char*         ok_tag_str =
            make_confirm_json(csilk_json_get_string(tag_obj, "draft_args_json"), tag_tok, NULL, NULL);
        char* tag_ok_res = ai_tool_dispatch(ctx, "confirm_proposed_tag", ok_tag_str);
        free(ok_tag_str);
        assert(tag_ok_res != NULL && strstr(tag_ok_res, "\"success\":true") != NULL);
        free(tag_ok_res);
        csilk_json_free(tag_obj);
        free(prop_tag);

        char* tags_list = ai_tool_dispatch(ctx, "get_tags", "{\"prefix\":\"出差\"}");
        assert(tags_list != NULL && strstr(tags_list, "出差报销") != NULL);
        free(tags_list);

        /* 13c. propose_savings_goal (create & update) -> confirm -> get_savings_goals */
        char* prop_goal = ai_tool_dispatch(
            ctx,
            "propose_savings_goal",
            "{\"name\":\"备用金\",\"target_amount\":50000,\"current_amount\":10000,\"deadline\":\"2027-12-31\"}");
        assert(prop_goal != NULL && strstr(prop_goal, "propose_success") != NULL);
        csilk_json_t* goal_obj = csilk_json_parse(prop_goal);
        const char*   goal_tok = csilk_json_get_string(goal_obj, "draft_token");
        char*         ok_goal_str = make_confirm_json(
            csilk_json_get_string(goal_obj, "draft_args_json"), goal_tok, NULL, NULL);
        char* goal_ok_res = ai_tool_dispatch(ctx, "confirm_proposed_savings_goal", ok_goal_str);
        free(ok_goal_str);
        assert(goal_ok_res != NULL && strstr(goal_ok_res, "\"success\":true") != NULL);
        csilk_json_t* goal_ok_parsed = csilk_json_parse(goal_ok_res);
        int64_t       goal_id = db_get_int(goal_ok_parsed, "id");
        assert(goal_id > 0);
        csilk_json_free(goal_ok_parsed);
        free(goal_ok_res);
        csilk_json_free(goal_obj);
        free(prop_goal);

        char* goals_list = ai_tool_dispatch(ctx, "get_savings_goals", "{}");
        assert(goals_list != NULL && strstr(goals_list, "备用金") != NULL);
        assert(strstr(goals_list, "\"progress_pct\":20") != NULL);
        assert(strstr(goals_list, "\"remaining_amount\":40000") != NULL);
        free(goals_list);

        /* Update existing savings goal */
        char upd_req[256];
        snprintf(upd_req,
                 sizeof(upd_req),
                 "{\"goal_id\":%lld,\"name\":\"备用金\",\"target_amount\":50000,\"current_amount\":50000}",
                 (long long)goal_id);
        char* prop_goal_upd = ai_tool_dispatch(ctx, "propose_savings_goal", upd_req);
        csilk_json_t* gupd_obj = csilk_json_parse(prop_goal_upd);
        char*         ok_gupd_str =
            make_confirm_json(csilk_json_get_string(gupd_obj, "draft_args_json"),
                              csilk_json_get_string(gupd_obj, "draft_token"),
                              NULL,
                              NULL);
        char* gupd_res = ai_tool_dispatch(ctx, "confirm_proposed_savings_goal", ok_gupd_str);
        free(ok_gupd_str);
        assert(gupd_res != NULL && strstr(gupd_res, "\"success\":true") != NULL);
        free(gupd_res);
        csilk_json_free(gupd_obj);
        free(prop_goal_upd);

        char* goals_open = ai_tool_dispatch(ctx, "get_savings_goals", "{\"include_completed\":false}");
        assert(goals_open != NULL && strstr(goals_open, "备用金") == NULL);
        free(goals_open);

        /* 13d. propose_transaction (buy stock funded by cash) -> confirm -> replay */
        assert(csilk_db_exec(pool,
                             "INSERT INTO categories (id, user_id, name, type, asset_type) "
                             "VALUES (20, 1, '股票投资', 'asset', 'stock')") == 0);
        assert(csilk_db_exec(pool,
                             "INSERT INTO assets (id, user_id, category_id, name, current_value, "
                             "quantity, cost_basis, net_value, currency, ledger_id) "
                             "VALUES (103, 1, 20, '沪深300ETF', 0.0, 0.0, 0.0, 1.0, 'CNY', 1)") == 0);
        /* Give 微信零钱 enough balance and bind ledger_id=1 so cross-ledger check aligns */
        assert(csilk_db_exec(pool, "UPDATE assets SET current_value=2000.0, ledger_id=1 WHERE id=102") == 0);

        char* prop_tx = ai_tool_dispatch(
            ctx,
            "propose_transaction",
            "{\"transaction_type\":\"buy\",\"amount\":1000.0,\"quantity\":10.0,"
            "\"asset_name\":\"沪深300ETF\",\"linked_asset_name\":\"微信零钱\",\"note\":\"定投买入\"}");
        assert(prop_tx != NULL && strstr(prop_tx, "propose_success") != NULL);
        csilk_json_t* tx_obj = csilk_json_parse(prop_tx);
        assert(db_get_num(tx_obj, "price") == 100.0);
        const char* tx_tok = csilk_json_get_string(tx_obj, "draft_token");
        char*       ok_tx_str =
            make_confirm_json(csilk_json_get_string(tx_obj, "draft_args_json"), tx_tok, NULL, NULL);

        char* tx_ok_res = ai_tool_dispatch(ctx, "confirm_proposed_transaction", ok_tx_str);
        assert(tx_ok_res != NULL && strstr(tx_ok_res, "\"success\":true") != NULL);
        free(tx_ok_res);

        char* tx_replay_res = ai_tool_dispatch(ctx, "confirm_proposed_transaction", ok_tx_str);
        free(ok_tx_str);
        assert(tx_replay_res != NULL && strstr(tx_replay_res, "\"error\"") != NULL);
        free(tx_replay_res);

        /* Verify linked cash debited (2000 - 1000 = 1000) and stock position updated */
        csilk_json_t* a102 = asset_get(pool, user_id, 0, 102);
        csilk_json_t* a103 = asset_get(pool, user_id, 0, 103);
        assert(a102 != NULL && csilk_json_array_size(a102) > 0);
        assert(a103 != NULL && csilk_json_array_size(a103) > 0);
        assert(db_get_num(csilk_json_array_get(a102, 0), "current_value") == 1000.0);
        assert(db_get_num(csilk_json_array_get(a103, 0), "quantity") == 10.0);
        csilk_json_free(a102);
        csilk_json_free(a103);

        csilk_json_free(tx_obj);
        free(prop_tx);

        printf("[Test 13] PASS: Proposal write tools lifecycle verified\n");
    }

    ai_tool_context_free(ctx);
    unlink(db_file);

    printf("🎉 ALL AI TOOL FRAMEWORK TESTS PASSED!\n");
    return 0;
}
