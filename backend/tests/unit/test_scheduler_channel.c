/*
 * Spec §9.2 dedicated unit suite for the scheduler notification-channel
 * use cases: scheduler_usecase_get_channel / save_channel / test_channel.
 *
 * Complements test_im_push_outbox.c::test_scheduler_ilink_channel (which
 * already covers the wechat_ilink closed loop and invalid type rejection)
 * by adding the previously untested branches:
 *   - secret-key preservation on resave (empty / masked / fresh)
 *   - secret masking shape in get_channel (short & long secrets)
 *   - per-user ownership isolation of the channel row
 *   - the no-row get_channel default shape
 *   - 'none' / unconfigured test_channel rejection
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include <pthread.h>

#include "application/ai/im_push_usecases.h"
#include "application/scheduler/dtos.h"
#include "application/scheduler/usecases.h"
#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include "domain/scheduler/repository.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_scheduler_channel.db";

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
setup_db(void)
{
    cleanup_db_files();

    config_secret_set_test_mode(true);
    config_secret_set_test_override("DB_DRIVER", "sqlite");
    config_secret_set_test_override("DB_DSN", TEST_DB_PATH);
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_sched_channel_32b!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);
    return pool;
}

static int64_t
seed_user_only(csilk_db_pool_t* pool, const char* username)
{
    assert(csilk_db_exec_param(pool,
                               "INSERT INTO users (username, password) VALUES (?, 'hash')",
                               (const char*[]){username, NULL}) == 0);
    csilk_json_t* ures = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username=? LIMIT 1", (const char*[]){username, NULL});
    assert(ures && csilk_json_array_size(ures) == 1);
    int64_t uid = db_get_int(csilk_json_array_get(ures, 0), "id");
    csilk_json_free(ures);
    return uid;
}

static int64_t
seed_user_and_active_instance(
    csilk_db_pool_t* pool, const char* username, const char* bot_id, const char* wx_user)
{
    int64_t uid = seed_user_only(pool, username);
    int64_t inst_id = mf_im_bot_instance_create(pool, uid, "wechat", "ilink");
    assert(inst_id > 0);
    char err[128] = {0};
    assert(mf_im_bot_instance_activate(pool, inst_id, bot_id, wx_user, err, sizeof(err)) == 0);
    return uid;
}

/* ---------- get_channel: default shape when no row exists ---------- */
static void
test_get_channel_default_shape(csilk_db_pool_t* pool)
{
    int64_t       u = seed_user_only(pool, "chan_default_u");
    csilk_json_t* resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &resp) == 0 && resp != NULL);
    assert(strcmp(csilk_json_get_string(resp, "channel_type"), "none") == 0);
    assert(strcmp(csilk_json_get_string(resp, "webhook_url"), "") == 0);
    assert(strcmp(csilk_json_get_string(resp, "secret_key_masked"), "") == 0);
    assert(db_get_int(resp, "is_active") == 0);
    csilk_json_free(resp);
    printf("  [OK] test_get_channel_default_shape\n");
}

/* ---------- get_channel: secret_key is never returned in clear ---------- */
static void
test_get_channel_masks_secret(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_only(pool, "chan_mask_u");
    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "serverchan 号");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://sct.fiend.xyz/send/SCT");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "abc"); /* 3-char secret */
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    csilk_json_t* resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &resp) == 0 && resp != NULL);
    assert(strcmp(csilk_json_get_string(resp, "channel_type"), "serverchan") == 0);
    assert(strcmp(csilk_json_get_string(resp, "secret_key_masked"), "******") == 0);
    csilk_json_free(resp);

    /* Long secret masks first-3 + last-3 with stars in between */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "pushdeer");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "pushdeer 号");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://push.deer");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "LONGSECRET456");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &resp) == 0 && resp != NULL);
    assert(strcmp(csilk_json_get_string(resp, "secret_key_masked"), "LON******456") == 0);
    csilk_json_free(resp);
    printf("  [OK] test_get_channel_masks_secret\n");
}

/* ---------- save_channel: credential-preservation semantics ---------- */
static void
test_save_channel_preserves_secret(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_only(pool, "chan_keep_u");

    /* Seed an existing secret on the channel */
    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://sct.fiend.xyz/send/SCT");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "ORIGSECRET");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    /* Resave with secret_key left empty -> original secret kept */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://sct.fiend.xyz/send/SCT");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    csilk_json_t* row = mf_scheduler_repo_get_user_channel(pool, u);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "secret_key"), "ORIGSECRET") == 0);
    csilk_json_free(row);

    /* Resave with masked secret (contains '******') -> original kept */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://sct.fiend.xyz/send/SCT");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "ORI******T");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    row = mf_scheduler_repo_get_user_channel(pool, u);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "secret_key"), "ORIGSECRET") == 0);
    csilk_json_free(row);

    /* Fresh non-masked secret replaces */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://sct.fiend.xyz/send/SCT");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "NEWSECRET");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    row = mf_scheduler_repo_get_user_channel(pool, u);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "secret_key"), "NEWSECRET") == 0);
    csilk_json_free(row);
    printf("  [OK] test_save_channel_preserves_secret\n");
}

/* ---------- save_channel: type allowlist & credential stripping ---------- */
static void
test_save_channel_type_rules(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_only(pool, "chan_type_u");
    channel_save_dto_t dto;

    /* Empty / unknown types -> 1002 */
    memset(&dto, 0, sizeof(dto));
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 1002);
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "unknown_type");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 1002);

    /* wechat_ilink strips webhook_url + secret_key (no-credential channel) */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "个人微信 iLink");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://leak.webhook");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "LEAKSECRET");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    csilk_json_t* row = mf_scheduler_repo_get_user_channel(pool, u);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "channel_type"), "wechat_ilink") == 0);
    assert(strcmp(csilk_json_get_string(row, "webhook_url"), "") == 0);
    assert(strcmp(csilk_json_get_string(row, "secret_key"), "") == 0);
    csilk_json_free(row);

    /* 'none' clears everything */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "none");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);
    row = mf_scheduler_repo_get_user_channel(pool, u);
    assert(row != NULL);
    assert(strcmp(csilk_json_get_string(row, "channel_type"), "none") == 0);
    assert(strcmp(csilk_json_get_string(row, "webhook_url"), "") == 0);
    assert(strcmp(csilk_json_get_string(row, "secret_key"), "") == 0);
    csilk_json_free(row);
    printf("  [OK] test_save_channel_type_rules\n");
}

/* ---------- Ownership: user A's channel row is invisible to user B ---------- */
static void
test_channel_ownership_isolation(csilk_db_pool_t* pool)
{
    int64_t u1 = seed_user_only(pool, "chan_own_a");
    int64_t u2 = seed_user_only(pool, "chan_own_b");

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "A-sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://a.sct");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "A-SECRET");
    assert(scheduler_usecase_save_channel(pool, u1, &dto) == 0);

    /* u2 sees the default shape, not u1's row */
    csilk_json_t* resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u2, &resp) == 0 && resp != NULL);
    assert(strcmp(csilk_json_get_string(resp, "channel_type"), "none") == 0);
    csilk_json_free(resp);

    /* u2 saves its own channel; u1's row is untouched */
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "serverchan");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "B-sc");
    snprintf(dto.webhook_url, sizeof(dto.webhook_url), "https://b.sct");
    snprintf(dto.secret_key, sizeof(dto.secret_key), "B-SECRET");
    assert(scheduler_usecase_save_channel(pool, u2, &dto) == 0);

    csilk_json_t* row1 = mf_scheduler_repo_get_user_channel(pool, u1);
    csilk_json_t* row2 = mf_scheduler_repo_get_user_channel(pool, u2);
    assert(row1 && strcmp(csilk_json_get_string(row1, "channel_name"), "A-sc") == 0);
    assert(row2 && strcmp(csilk_json_get_string(row2, "channel_name"), "B-sc") == 0);
    assert(strcmp(csilk_json_get_string(row1, "secret_key"), "A-SECRET") == 0);
    assert(strcmp(csilk_json_get_string(row2, "secret_key"), "B-SECRET") == 0);
    csilk_json_free(row1);
    csilk_json_free(row2);
    printf("  [OK] test_channel_ownership_isolation\n");
}

/* ---------- test_channel: unconfigured / 'none' rejection paths ---------- */
static void
test_test_channel_unconfigured(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_only(pool, "chan_test_unconf");
    char    err[256] = {0};
    /* No channel row at all -> non-zero with a clear message */
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);
    assert(err[0] != '\0');

    /* 'none' channel (explicitly saved) -> also rejected */
    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "none");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);
    err[0] = '\0';
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);

    /* NULL pool never crashes */
    assert(scheduler_usecase_test_channel(NULL, u, err, sizeof(err)) != 0);
    printf("  [OK] test_test_channel_unconfigured\n");
}

/* ---------- test_channel: wechat_ilink closed loop with gateway ack ---------- */
typedef struct {
    csilk_db_pool_t* pool;
    int64_t          inst_id;
    const char*      ack_result;
    const char*      ack_err;
} ilink_ack_ctx_t;

static void*
ilink_ack_worker(void* arg)
{
    ilink_ack_ctx_t*   w = (ilink_ack_ctx_t*)arg;
    const char*        res = w->ack_result ? w->ack_result : "sent";
    const char*        aerr = w->ack_err ? w->ack_err : "";
    for (int i = 0; i < 40; ++i) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        if (im_push_usecase_gateway_claim(w->pool,
                                          w->inst_id,
                                          true,
                                          "2026-11-25T13:00:00Z",
                                          5,
                                          &items,
                                          &code,
                                          err,
                                          sizeof(err)) == 0 &&
            items && csilk_json_array_size(items) > 0) {
            csilk_json_t* row = csilk_json_array_get(items, 0);
            int64_t       pid = db_get_int(row, "id");
            assert(im_push_usecase_gateway_ack(
                       w->pool, w->inst_id, pid, res, aerr, 0, &code, err, sizeof(err)) == 0);
            csilk_json_free(items);
            return NULL;
        }
        if (items) {
            csilk_json_free(items);
        }
        struct timespec req = {0, 25000000L}; /* 25ms */
        nanosleep(&req, NULL);
    }
    return NULL;
}

static void
test_test_channel_wechat_ilink_closed_loop(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_and_active_instance(pool, "chan_test_ilink_u", "bot_sc1", "wx_sc1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "个人微信 iLink");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    /* Gateway acks 'sent' inside the poll window -> rc == 0 */
    ilink_ack_ctx_t ctx = {.pool = pool, .inst_id = inst_id, .ack_result = "sent"};
    pthread_t       tid;
    assert(pthread_create(&tid, NULL, ilink_ack_worker, &ctx) == 0);
    char err[256] = {0};
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) == 0);
    assert(err[0] == '\0');
    pthread_join(tid, NULL);

    /* Gateway acks 'failed' with a message -> propagates last_error */
    ilink_ack_ctx_t fctx = {
        .pool = pool, .inst_id = inst_id, .ack_result = "failed", .ack_err = "网关投递被拒"};
    assert(pthread_create(&tid, NULL, ilink_ack_worker, &fctx) == 0);
    err[0] = '\0';
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);
    assert(strcmp(err, "网关投递被拒") == 0);
    pthread_join(tid, NULL);

    printf("  [OK] test_test_channel_wechat_ilink_closed_loop\n");
}

int
main(void)
{
    printf("=== Starting scheduler channel usecase Unit Tests ===\n");
    csilk_db_pool_t* pool = setup_db();

    test_get_channel_default_shape(pool);
    test_get_channel_masks_secret(pool);
    test_save_channel_preserves_secret(pool);
    test_save_channel_type_rules(pool);
    test_channel_ownership_isolation(pool);
    test_test_channel_unconfigured(pool);
    test_test_channel_wechat_ilink_closed_loop(pool);

    csilk_db_pool_free(pool);
    cleanup_db_files();
    printf("=== scheduler channel usecase Unit Tests Passed! ===\n");
    return 0;
}
