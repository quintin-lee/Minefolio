/** iLink 微信对话记账：能力开关、短码草案与确认端点单测。 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "application/ai/im_bot_instance_usecases.h"
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include "infrastructure/repositories/ai_session_repo_impl.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "services/ai/policy/confirmation.h"
#include "services/ai/tools/registry.h"

static const char* TEST_DB = "/tmp/test_mf_im_bookkeeping.db";

static void
cleanup_db(void)
{
    unlink(TEST_DB);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s-shm", TEST_DB);
    unlink(buf);
    snprintf(buf, sizeof(buf), "%s-wal", TEST_DB);
    unlink(buf);
}

static csilk_db_pool_t*
setup_db(void)
{
    cleanup_db();
    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB);
    config_secret_set_test_override("JWT_SECRET", "test_im_bookkeeping_jwt_secret_32b!");
    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);
    ai_tool_registry_init();
    return pool;
}

static int64_t
seed_user_instance(csilk_db_pool_t* pool, int64_t uid, const char* wx)
{
    char uid_s[32], username[64], cat_s[32], asset_s[32];
    snprintf(uid_s, sizeof(uid_s), "%lld", (long long)uid);
    snprintf(username, sizeof(username), "im_book_user_%lld", (long long)uid);
    snprintf(cat_s, sizeof(cat_s), "%lld", (long long)(1000 + uid));
    snprintf(asset_s, sizeof(asset_s), "%lld", (long long)(2000 + uid));
    csilk_json_free(csilk_db_query_param_json(
        pool, "INSERT INTO users (id, username, password) VALUES (?, ?, 'hash')",
        (const char*[]){uid_s, username, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (id, owner_id, name, is_default) VALUES (1, ?, '默认账本', 1)",
        (const char*[]){uid_s, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (1, ?, 'owner')",
        (const char*[]){uid_s, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool, "INSERT INTO categories (id, user_id, name, type, icon) "
              "VALUES (?, ?, '餐饮美食', 'expense', '')",
        (const char*[]){cat_s, uid_s, NULL}));
    csilk_json_free(csilk_db_query_param_json(
        pool, "INSERT INTO assets (id, user_id, category_id, name, current_value, currency, ledger_id) "
              "VALUES (?, ?, ?, '现金钱包', 1000.0, 'CNY', 1)",
        (const char*[]){asset_s, uid_s, cat_s, NULL}));

    int64_t inst = mf_im_bot_instance_create(pool, uid, "wechat", "ilink");
    assert(inst > 0);
    char bot[64], err[256] = {0};
    snprintf(bot, sizeof(bot), "bot_%lld", (long long)uid);
    assert(mf_im_bot_instance_activate(pool, inst, bot, wx, err, sizeof(err)) == 0);
    return inst;
}

static void
test_capability_switch(csilk_db_pool_t* pool)
{
    int64_t inst = seed_user_instance(pool, 911, "wx_cap");
    csilk_json_t* row = mf_im_bot_instance_get(pool, 911, inst);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "capability"), "readonly") == 0);
    csilk_json_free(row);

    /* 非 owner 不能改能力档 */
    assert(mf_im_bot_instance_set_capability(pool, 912, inst, "query+confirm") == -3);
    assert(mf_im_bot_instance_set_capability(pool, 911, inst, "query+confirm") == 0);

    row = mf_im_bot_instance_get(pool, 911, inst);
    assert(row != NULL && strcmp(csilk_json_get_string(row, "capability"), "query+confirm") == 0);
    csilk_json_free(row);

    char scope[32] = {0};
    int64_t bound = mf_im_bot_repo_lookup(pool, "wechat", "ilink:wx_cap", scope, sizeof(scope));
    assert(bound == 911 && strcmp(scope, "query+confirm") == 0);

    /* 重新登录/激活不应把能力档重置为 readonly */
    assert(mf_im_bot_instance_relogin(pool, 911, inst) == 0);
    char err[256] = {0};
    assert(mf_im_bot_instance_activate(pool, inst, "bot_cap2", "wx_cap", err, sizeof(err)) == 0);
    row = mf_im_bot_instance_get(pool, 911, inst);
    assert(row != NULL && strcmp(csilk_json_get_string(row, "capability"), "query+confirm") == 0);
    csilk_json_free(row);

    printf("[Test iLink Capability] PASS: default readonly, owner-only toggle, survives relogin\n");
}

static void
test_pending_action_lifecycle(csilk_db_pool_t* pool)
{
    int64_t now = (int64_t)time(NULL);
    assert(mf_im_bot_pending_action_insert(pool,
                                           "CODE1234",
                                           911,
                                           77,
                                           "daily_expense",
                                           "mf_v2.dummy",
                                           "{\"amount\":12.34}",
                                           now + 300) == 0);
    csilk_json_t* row =
        mf_im_bot_pending_action_get(pool, "CODE1234", 911, 77, now);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "draft_token"), "mf_v2.dummy") == 0);
    csilk_json_free(row);

    /* 过期后查不到 */
    assert(mf_im_bot_pending_action_get(pool, "CODE1234", 911, 77, now + 301) == NULL);
    /* 一次性消费，重放失败 */
    assert(mf_im_bot_pending_action_consume(pool, "CODE1234", 911, 77) == 0);
    assert(mf_im_bot_pending_action_get(pool, "CODE1234", 911, 77, now) == NULL);
    assert(mf_im_bot_pending_action_consume(pool, "CODE1234", 911, 77) != 0);
    printf("[Test iLink Pending Action] PASS: expiry, single-use consume and replay protection\n");
}

static void
test_confirm_endpoint(csilk_db_pool_t* pool)
{
    const int64_t uid = 912;
    const char*   wx = "wx_confirm";
    const char*   im_uid = "ilink:wx_confirm";
    int64_t       inst = seed_user_instance(pool, uid, wx);
    int64_t       session = mf_ai_session_insert(
        pool, uid, "IM: wechat@ilink:wx_confirm", "im-bot", "system");
    assert(session > 0);

    csilk_json_t* args = csilk_json_object();
    csilk_json_add_string(args, "type", "expense");
    csilk_json_add_number(args, "amount", 12.34);
    csilk_json_add_string(args, "date", "2026-11-26");
    csilk_json_add_number(args, "category_id", 1000 + uid);
    csilk_json_add_number(args, "asset_id", 2000 + uid);
    csilk_json_add_string(args, "note", "午餐");
    char* args_json = csilk_json_serialize(args, NULL);
    assert(args_json != NULL);
    char* token = ai_confirmation_create_bound_token(
        uid, session, "confirm_proposed_expense", args, AI_RISK_HIGH, 300);
    assert(token != NULL);
    int64_t now = (int64_t)time(NULL);
    assert(mf_im_bot_pending_action_insert(pool, "CONF1234", uid, session, "daily_expense",
                                           token, args_json, now + 300) == 0);

    char* reply = NULL;
    int   code = 0;
    char  err[256] = {0};

    /* 默认 readonly：即使有短码也必须拒绝 */
    assert(im_instance_gateway_confirm(
               pool, inst, im_uid, "CONF1234", &reply, &code, err, sizeof(err)) != 0);
    assert(code == 1004 && reply == NULL);

    assert(mf_im_bot_instance_set_capability(pool, uid, inst, "query+confirm") == 0);

    /* 错发送者 / 错短码拒绝 */
    assert(im_instance_gateway_confirm(
               pool, inst, "ilink:someone_else", "CONF1234", &reply, &code, err, sizeof(err)) != 0);
    assert(im_instance_gateway_confirm(
               pool, inst, im_uid, "WRONGCOD", &reply, &code, err, sizeof(err)) != 0);

    /* 正确短码落账（asset_id=0，跳过余额联动） */
    int rc_ok = im_instance_gateway_confirm(
        pool, inst, im_uid, "CONF1234", &reply, &code, err, sizeof(err));
    if (rc_ok != 0) {
        fprintf(stderr, "DEBUG confirm failed code=%d err=%s\n", code, err);
    }
    assert(rc_ok == 0);
    assert(reply != NULL && strstr(reply, "已记账") != NULL);
    free(reply);
    reply = NULL;

    /* 重放被拒绝 */
    assert(im_instance_gateway_confirm(
               pool, inst, im_uid, "CONF1234", &reply, &code, err, sizeof(err)) != 0);
    assert(reply == NULL);

    csilk_json_free(args);
    free(args_json);
    free(token);
    printf("[Test iLink Confirm Endpoint] PASS: capability/sender/code checks, booking and replay\n");
}

int
main(void)
{
    printf("=== Running iLink Bookkeeping Tests ===\n");
    csilk_db_pool_t* pool = setup_db();
    test_capability_switch(pool);
    test_pending_action_lifecycle(pool);
    test_confirm_endpoint(pool);
    cleanup_db();
    printf("ALL ILINK BOOKKEEPING TESTS PASSED!\n");
    return 0;
}
