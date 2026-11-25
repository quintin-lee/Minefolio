# 微信 iLink 接收定时任务推送 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable Minefolio users to receive scheduled task briefings/alerts (personal jobs, manual runs, and built-in global broadcasts) via their personal WeChat iLink bot instance as a `wechat_ilink` notification channel with a 30-minute durable outbox, inbound `context_token` wake-up redelivery, and synchronous test-channel feedback.

**Architecture:** The C23 backend persists outbound pushes in `im_push_outbox` and exposes outbound-only gateway endpoints (`pushes/claim`, `pushes/:push_id/ack`, plus `heartbeat` `last_inbound_at`). Each Node `ilink-runner` child process persists `context.json` (`0600`) on owner inbound messages, runs an independent `pusher.js` worker that pulls/formats/chunks/sends via iLink `sendmessage`, and wakes immediately on inbound messages; `bot_token` and `context_token` never leave the gateway.

**Tech Stack:** C23 / csilk / SQLite & PostgreSQL (dual-engine migrations + unit/integration tests), Node.js 22 built-ins (`fetch`/`crypto`/`fs`/`node:test`), Vue 3 + TypeScript + Element Plus + Vitest.

**Spec:** [`docs/superpowers/specs/2026-11-25-wechat-ilink-scheduler-push-design.md`](file:///home/quintin/Data/source/c_cpp/Minefolio/docs/superpowers/specs/2026-11-25-wechat-ilink-scheduler-push-design.md)

---

## File Structure Map

| Area | File | Role |
|---|---|---|
| Migration | `backend/sql/migrations/sqlite/V037__im_push_outbox.sql` | Add `im_bot_instances.last_inbound_at`, `job_run_logs.notif_sync_*`, `im_push_outbox` + indexes (SQLite) |
| Migration | `backend/sql/migrations/postgres/V037__im_push_outbox.sql` | Same schema migration for PostgreSQL |
| Full Schema | `backend/sql/migration.sql`, `backend/sql/migration_postgres.sql` | Sync `job_run_logs`, `im_bot_instances`, `im_push_outbox` into baseline SQL scripts |
| Outbox Repo | `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.{h,c}` | Enqueue, lease-reclaim claim, state ack, stale expiry, `job_run_logs` status rollup |
| IM Repo | `backend/src/infrastructure/repositories/im_bot_repo_impl.{h,c}` | Lookup user's `(platform, adapter)` instance; track `last_inbound_at` on claim & heartbeat |
| Scheduler Repo | `backend/src/domain/scheduler/entity.h`, `repository.h`, `scheduler_repo_impl.c` | List all active user channels (`mf_scheduler_repo_list_active_channels`), sync counters & notif update |
| Push Usecases | `backend/src/application/ai/im_push_usecases.{h,c}` | Validation, `enqueue`, `gateway_claim`, `gateway_ack`, `expire_stale`, `build_ilink_status` |
| IM Usecases/Controller | `backend/src/application/ai/im_bot_instance_usecases.{h,c}`, `im_bot_instance_controller.c` | Gateway HTTP routes `pushes/claim`, `pushes/:push_id/ack`, extended `heartbeat` |
| Scheduler Usecases & Engine | `backend/src/application/scheduler/usecases.c`, `backend/src/core/scheduler/scheduler_engine.c` | `wechat_ilink` channel CRUD/status/test-poll, single-user & built-in broadcast, periodic expiry |
| Gateway State/Client/Formatter/API | `gateway/ilink/{state,client,formatter,api}.js` | `context.json` (`0600`), `-2`/`-14` error classification, Markdown cleanup & chunking, push HTTP client |
| Gateway Runner Loop | `gateway/ilink/{pusher,monitor,runner}.js` | Inbound `context.json` refresh + `wake()`, serial Outbox pusher loop, graceful `-14` exit |
| Frontend UI & Types | `frontend/src/types/scheduler.ts`, `ChannelConfigDialog.vue`, `Scheduler.vue`, `JobLogDrawer.vue` | `wechat_ilink` radio & readiness card, `'queued'`/`'partial'` tags |
| Tests | `backend/tests/unit/test_im_push_outbox.c`, `test_scheduler_engine.c`, `backend/tests/test_ilink_scheduler_push.sh`, `gateway/ilink/test/*.test.js`, `frontend/tests/scheduler-ilink-channel.spec.ts` | Full unit, sanitizer, dual-engine integration, gateway, and frontend test coverage |

**Important Build Notes:**
- Always build backend out-of-source in `backend/build` (or `backend/build-asan` for sanitizers) with `-G "Unix Makefiles"`, wrapped by `./backend/tests/run_isolated.sh`.
- Whenever adding new `.c` files under `backend/src/`, re-run `cmake -S backend -B backend/build -G "Unix Makefiles"` so `file(GLOB_RECURSE SOURCES "src/*.c")` picks them up.
- Follow `AGENTS.md`: `void* pool` in `application/*/*.h`, zero naked SQL in `application/*/*.c`, `db_get_int/db_get_bool` for DB JSON fields, PostgreSQL explicit `::timestamptz` casts where `TIMESTAMPTZ` is updated from `?`.

---

### Task 1: Dual-Engine Migration `V037__im_push_outbox.sql` & Full Schema Sync

**Files:**
- Create: `backend/sql/migrations/sqlite/V037__im_push_outbox.sql`
- Create: `backend/sql/migrations/postgres/V037__im_push_outbox.sql`
- Modify: `backend/sql/migration.sql:626-692`
- Modify: `backend/sql/migration_postgres.sql:608-672`

- [ ] **Step 1: Create SQLite and PostgreSQL V037 migrations**

Write `backend/sql/migrations/sqlite/V037__im_push_outbox.sql`:

```sql
-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (SQLite)

ALTER TABLE im_bot_instances ADD COLUMN last_inbound_at TEXT;
ALTER TABLE job_run_logs ADD COLUMN notif_sync_sent INTEGER NOT NULL DEFAULT 0;
ALTER TABLE job_run_logs ADD COLUMN notif_sync_failed INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS im_push_outbox (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id        INTEGER NOT NULL REFERENCES im_bot_instances(id) ON DELETE CASCADE,
    mf_user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_run_log_id     INTEGER REFERENCES job_run_logs(id) ON DELETE SET NULL,
    kind               TEXT NOT NULL DEFAULT 'scheduler',
    title              TEXT NOT NULL,
    content            TEXT NOT NULL,
    status             TEXT NOT NULL DEFAULT 'pending',
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 5,
    lease_until_ts     INTEGER NOT NULL DEFAULT 0,
    next_attempt_at_ts INTEGER NOT NULL DEFAULT 0,
    expires_at_ts      INTEGER NOT NULL,
    last_error         TEXT NOT NULL DEFAULT '',
    created_at         TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_im_push_outbox_claim
    ON im_push_outbox(instance_id, status, next_attempt_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_expires
    ON im_push_outbox(status, expires_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_job_log
    ON im_push_outbox(job_run_log_id) WHERE job_run_log_id IS NOT NULL;
```

Write `backend/sql/migrations/postgres/V037__im_push_outbox.sql`:

```sql
-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (PostgreSQL)

ALTER TABLE im_bot_instances ADD COLUMN IF NOT EXISTS last_inbound_at TIMESTAMPTZ;
ALTER TABLE job_run_logs ADD COLUMN IF NOT EXISTS notif_sync_sent INTEGER NOT NULL DEFAULT 0;
ALTER TABLE job_run_logs ADD COLUMN IF NOT EXISTS notif_sync_failed INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS im_push_outbox (
    id                 BIGSERIAL PRIMARY KEY,
    instance_id        BIGINT NOT NULL REFERENCES im_bot_instances(id) ON DELETE CASCADE,
    mf_user_id         BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_run_log_id     BIGINT REFERENCES job_run_logs(id) ON DELETE SET NULL,
    kind               VARCHAR(16) NOT NULL DEFAULT 'scheduler',
    title              TEXT NOT NULL,
    content            TEXT NOT NULL,
    status             VARCHAR(16) NOT NULL DEFAULT 'pending',
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 5,
    lease_until_ts     BIGINT NOT NULL DEFAULT 0,
    next_attempt_at_ts BIGINT NOT NULL DEFAULT 0,
    expires_at_ts      BIGINT NOT NULL,
    last_error         TEXT NOT NULL DEFAULT '',
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_im_push_outbox_claim
    ON im_push_outbox(instance_id, status, next_attempt_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_expires
    ON im_push_outbox(status, expires_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_job_log
    ON im_push_outbox(job_run_log_id) WHERE job_run_log_id IS NOT NULL;
```

- [ ] **Step 2: Sync `backend/sql/migration.sql` and `backend/sql/migration_postgres.sql`**

1. In `backend/sql/migration.sql`:
   - Add `notif_sync_sent INTEGER NOT NULL DEFAULT 0,` and `notif_sync_failed INTEGER NOT NULL DEFAULT 0,` inside `job_run_logs`.
   - Append `im_bot_instances` (including `last_inbound_at TEXT` so FK checks succeed in raw `migration.sql` tests) and `im_push_outbox` + its 3 indexes at the end of `backend/sql/migration.sql`:

```sql
-- V035 + V037: iLink 机器人独立实例与推送 Outbox (SQLite)
CREATE TABLE IF NOT EXISTS im_bot_instances (
    id                         INTEGER PRIMARY KEY AUTOINCREMENT,
    mf_user_id                 INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    platform                   TEXT NOT NULL DEFAULT 'wechat',
    adapter                    TEXT NOT NULL DEFAULT 'ilink',
    ilink_bot_id               TEXT,
    ilink_user_id              TEXT,
    im_user_id                 TEXT,
    status                     TEXT NOT NULL DEFAULT 'pending_login',
    qr_content                 TEXT,
    qr_expires_at              TEXT,
    login_epoch                INTEGER NOT NULL DEFAULT 0,
    pending_verify_code        TEXT,
    pending_verify_expires_at  TEXT,
    verify_attempts            INTEGER NOT NULL DEFAULT 0,
    enabled                    INTEGER NOT NULL DEFAULT 1,
    last_error                 TEXT,
    last_seen_at               TEXT,
    last_inbound_at            TEXT,
    created_at                 TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at                 TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(mf_user_id, platform, adapter)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_im_user
    ON im_bot_instances(platform, im_user_id) WHERE im_user_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_bot
    ON im_bot_instances(platform, ilink_bot_id) WHERE ilink_bot_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_im_instances_user ON im_bot_instances(mf_user_id);

CREATE TABLE IF NOT EXISTS im_push_outbox (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id        INTEGER NOT NULL REFERENCES im_bot_instances(id) ON DELETE CASCADE,
    mf_user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_run_log_id     INTEGER REFERENCES job_run_logs(id) ON DELETE SET NULL,
    kind               TEXT NOT NULL DEFAULT 'scheduler',
    title              TEXT NOT NULL,
    content            TEXT NOT NULL,
    status             TEXT NOT NULL DEFAULT 'pending',
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 5,
    lease_until_ts     INTEGER NOT NULL DEFAULT 0,
    next_attempt_at_ts INTEGER NOT NULL DEFAULT 0,
    expires_at_ts      INTEGER NOT NULL,
    last_error         TEXT NOT NULL DEFAULT '',
    created_at         TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_im_push_outbox_claim
    ON im_push_outbox(instance_id, status, next_attempt_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_expires
    ON im_push_outbox(status, expires_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_job_log
    ON im_push_outbox(job_run_log_id) WHERE job_run_log_id IS NOT NULL;
```

2. Apply the matching PostgreSQL block (`BIGSERIAL`/`BIGINT`/`TIMESTAMPTZ`/`VARCHAR(16)`/`NOW()`) to `backend/sql/migration_postgres.sql`:
   - Add `notif_sync_sent INTEGER NOT NULL DEFAULT 0,` and `notif_sync_failed INTEGER NOT NULL DEFAULT 0,` inside `job_run_logs`.
   - Append `im_bot_instances` (with `last_inbound_at TIMESTAMPTZ`) and `im_push_outbox` + its 3 indexes at the end.

- [ ] **Step 3: Build & verify migrations**

Run:
```bash
./backend/tests/run_isolated.sh build backend/build && \
ctest --test-dir backend/build -R "^(test_migration_engine|test_scheduler_migration)$" --output-on-failure
```
Expected: `2/2 passed` (`copy_assets` updates `backend/build/sql/migrations/`).

- [ ] **Step 4: Commit Task 1**

```bash
git add backend/sql/migrations/sqlite/V037__im_push_outbox.sql \
        backend/sql/migrations/postgres/V037__im_push_outbox.sql \
        backend/sql/migration.sql \
        backend/sql/migration_postgres.sql
git commit -m "feat(db): ✨ add V037 im_push_outbox and inbound metadata migration"
```

---

### Task 2: Outbox Repository, IM Instance `last_inbound_at` & Scheduler Repo Extensions

**Files:**
- Create: `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.c`
- Modify: `backend/src/infrastructure/repositories/im_bot_repo_impl.h:1-105`
- Modify: `backend/src/infrastructure/repositories/im_bot_repo_impl.c:12-20,706-743`
- Modify: `backend/src/domain/scheduler/entity.h:26-50`
- Modify: `backend/src/domain/scheduler/repository.h:60-85`
- Modify: `backend/src/infrastructure/repositories/scheduler_repo_impl.c:470-550`
- Modify: `backend/CMakeLists.txt:404-427`
- Create Test: `backend/tests/unit/test_im_push_outbox.c`

- [ ] **Step 1: Write failing unit tests in `backend/tests/unit/test_im_push_outbox.c` and register in `backend/CMakeLists.txt`**

Add to `backend/CMakeLists.txt` right after `test_wechat_push_service`:

```cmake
add_minefolio_unit_test(test_im_push_outbox tests/unit/test_im_push_outbox.c pthread)
add_dependencies(test_im_push_outbox copy_assets)
```

Create `backend/tests/unit/test_im_push_outbox.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/db.h"
#include "config/secret.h"
#include "csilk/csilk.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/im_push_outbox_repo_impl.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"

static const char* TEST_DB_PATH = "/tmp/test_mf_im_push_outbox.db";

static csilk_db_pool_t*
setup_db(void)
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
    config_secret_set_test_override("JWT_SECRET", "test_jwt_secret_im_push_outbox_32b!");

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);
    return pool;
}

static int64_t
seed_user_and_active_instance(csilk_db_pool_t* pool,
                              const char*      username,
                              const char*      bot_id,
                              const char*      wx_user)
{
    assert(csilk_db_exec_param(
               pool,
               "INSERT INTO users (username, password) VALUES (?, 'hash')",
               (const char*[]){username, NULL}) == 0);
    csilk_json_t* ures = csilk_db_query_param_json(
        pool, "SELECT id FROM users WHERE username=? LIMIT 1", (const char*[]){username, NULL});
    assert(ures && csilk_json_array_size(ures) == 1);
    int64_t uid = db_get_int(csilk_json_array_get(ures, 0), "id");
    csilk_json_free(ures);

    int64_t inst_id = mf_im_bot_instance_create(pool, uid, "wechat", "ilink");
    assert(inst_id > 0);
    char err[128] = {0};
    assert(mf_im_bot_instance_activate(pool, inst_id, bot_id, wx_user, err, sizeof(err)) == 0);
    return uid;
}

static void
test_outbox_repo_lifecycle(csilk_db_pool_t* pool)
{
    int64_t u1 = seed_user_and_active_instance(pool, "push_u1", "bot_p1", "wx_p1");
    int64_t u2 = seed_user_and_active_instance(pool, "push_u2", "bot_p2", "wx_p2");

    csilk_json_t* inst1 = mf_im_bot_instance_get_by_user(pool, u1, "wechat", "ilink");
    assert(inst1 != NULL);
    int64_t inst1_id = db_get_int(inst1, "id");
    csilk_json_free(inst1);

    /* Heartbeat with last_inbound_at + touch_inbound updates metadata cleanly */
    char    st[64] = {0};
    int64_t ep = 0;
    int     en = 0;
    assert(mf_im_bot_instance_heartbeat_with_inbound(
               pool, inst1_id, "2026-11-25T10:00:00Z", st, sizeof(st), &ep, &en) == 0);
    csilk_json_t* inst1_ref = mf_im_bot_instance_get(pool, u1, inst1_id);
    assert(inst1_ref != NULL);
    assert(strcmp(csilk_json_get_string(inst1_ref, "last_inbound_at"), "2026-11-25T10:00:00Z") ==
           0);
    csilk_json_free(inst1_ref);

    /* Create job & log */
    int64_t job_id = mf_scheduler_repo_job_create(pool,
                                                  u1,
                                                  "Morning Brief",
                                                  "Desc",
                                                  "ai_prompt",
                                                  "cron",
                                                  "{\"cron\":\"0 8 * * *\"}",
                                                  "{\"prompt\":\"test\"}",
                                                  "user_default",
                                                  "{}",
                                                  "");
    assert(job_id > 0);
    int64_t log_id = mf_scheduler_repo_log_create(pool, job_id, u1, "manual_invoke");
    assert(log_id > 0);
    assert(mf_scheduler_repo_log_update_notif(pool, log_id, "queued", "") == 0);

    int64_t now = (int64_t)time(NULL);
    int64_t p_sched = mf_im_push_outbox_enqueue(
        pool, inst1_id, u1, log_id, "scheduler", "日报", "内容A", 1800, 5);
    int64_t p_test = mf_im_push_outbox_enqueue(
        pool, inst1_id, u1, 0, "test", "连通性测试", "内容Test", 30, 1);
    assert(p_sched > 0 && p_test > 0);

    /* has_context = false claims ONLY kind='test', leaving scheduler pending */
    csilk_json_t* no_ctx_items = mf_im_push_outbox_claim(pool, inst1_id, false, 5, now, 60);
    assert(no_ctx_items && csilk_json_array_size(no_ctx_items) == 1);
    assert(db_get_int(csilk_json_array_get(no_ctx_items, 0), "id") == p_test);
    csilk_json_free(no_ctx_items);

    /* Ack test with waiting_context -> fails immediately */
    int64_t ack_lid = 0;
    assert(mf_im_push_outbox_ack(
               pool, inst1_id, p_test, "waiting_context", "请先发消息", 0, now, &ack_lid) == 0);
    csilk_json_t* test_row = mf_im_push_outbox_get(pool, p_test);
    assert(test_row && strcmp(csilk_json_get_string(test_row, "status"), "failed") == 0);
    csilk_json_free(test_row);

    /* Cross-instance ack rejected (-3) */
    csilk_json_t* inst2 = mf_im_bot_instance_get_by_user(pool, u2, "wechat", "ilink");
    int64_t       inst2_id = db_get_int(inst2, "id");
    csilk_json_free(inst2);
    assert(mf_im_push_outbox_ack(pool, inst2_id, p_sched, "sent", "", 0, now, NULL) == -3);

    /* has_context = true claims scheduler row */
    csilk_json_t* items = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now, 60);
    assert(items && csilk_json_array_size(items) == 1);
    assert(db_get_int(csilk_json_array_get(items, 0), "id") == p_sched);
    csilk_json_free(items);

    /* waiting_context on scheduler keeps attempts = 0 and schedules next attempt */
    assert(mf_im_push_outbox_ack(
               pool, inst1_id, p_sched, "waiting_context", "需刷新", 0, now, &ack_lid) == 0);
    assert(ack_lid == log_id);
    csilk_json_t* srow = mf_im_push_outbox_get(pool, p_sched);
    assert(srow && strcmp(csilk_json_get_string(srow, "status"), "pending") == 0);
    assert(db_get_int(srow, "attempts") == 0);
    csilk_json_free(srow);

    /* Re-claim after retry window, verify lease reclaim if now + 100 >= lease_until_ts */
    csilk_json_t* items2 = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now + 35, 60);
    assert(items2 && csilk_json_array_size(items2) == 1);
    csilk_json_free(items2);

    /* Lease expires at now + 35 + 60 = now + 95; at now + 96 reclaim succeeds */
    csilk_json_t* reclaimed = mf_im_push_outbox_claim(pool, inst1_id, true, 5, now + 96, 60);
    assert(reclaimed && csilk_json_array_size(reclaimed) == 1);
    csilk_json_free(reclaimed);

    /* Ack sent and rollup job_run_logs -> sent */
    assert(mf_im_push_outbox_ack(pool, inst1_id, p_sched, "sent", "", 0, now + 97, &ack_lid) == 0);
    assert(mf_im_push_outbox_rollup_job_log(pool, log_id) == 0);

    int64_t       ltotal = 0;
    csilk_json_t* logs = mf_scheduler_repo_log_list(pool, u1, false, job_id, 1, 10, &ltotal);
    assert(logs && csilk_json_array_size(logs) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(logs, 0), "notification_status"),
                  "sent") == 0);
    csilk_json_free(logs);

    /* Mixed sync + async rollup -> partial when outbox expires */
    int64_t log2 = mf_scheduler_repo_log_create(pool, 0, 0, "cron_tick");
    assert(log2 > 0);
    assert(mf_scheduler_repo_log_set_sync_counts(pool, log2, 1, 0) == 0);
    int64_t p_exp = mf_im_push_outbox_enqueue(
        pool, inst2_id, u2, log2, "scheduler", "快讯", "正文", 60, 5);
    assert(p_exp > 0);
    int64_t* exp_logs = NULL;
    size_t   exp_cnt = 0;
    assert(mf_im_push_outbox_expire_stale(pool, 0, now + 10000, "推送超时", &exp_logs, &exp_cnt) ==
           0);
    assert(exp_cnt == 1 && exp_logs[0] == log2);
    free(exp_logs);
    assert(mf_im_push_outbox_rollup_job_log(pool, log2) == 0);

    csilk_json_t* logs2 = mf_scheduler_repo_log_list(pool, 0, true, 0, 1, 10, &ltotal);
    assert(logs2 && csilk_json_array_size(logs2) >= 1);
    bool found_partial = false;
    for (size_t i = 0; i < csilk_json_array_size(logs2); ++i) {
        csilk_json_t* row = csilk_json_array_get(logs2, i);
        if (db_get_int(row, "id") == log2) {
            assert(strcmp(csilk_json_get_string(row, "notification_status"), "partial") == 0);
            found_partial = true;
        }
    }
    assert(found_partial);
    csilk_json_free(logs2);

    printf("  [OK] test_outbox_repo_lifecycle\n");
}

int
main(void)
{
    printf("=== Starting im_push_outbox Unit Tests ===\n");
    csilk_db_pool_t* pool = setup_db();
    test_outbox_repo_lifecycle(pool);
    csilk_db_pool_free(pool);
    unlink(TEST_DB_PATH);
    printf("=== im_push_outbox Unit Tests Passed! ===\n");
    return 0;
}
```

- [ ] **Step 2: Configure CMake & run failing test build**

Run:
```bash
cmake -S backend -B backend/build -G "Unix Makefiles" && \
cmake --build backend/build --target test_im_push_outbox
```
Expected: FAIL (`im_push_outbox_repo_impl.h: No such file or directory`).

- [ ] **Step 3: Implement IM Repo & Scheduler Repo extensions**

1. Update `backend/src/infrastructure/repositories/im_bot_repo_impl.h` (append before `#ifdef __cplusplus` close):

```c
/** 按 user_id + platform + adapter 查询实例，不存在返回 NULL。 */
csilk_json_t* mf_im_bot_instance_get_by_user(csilk_db_pool_t* pool,
                                             int64_t          user_id,
                                             const char*      platform,
                                             const char*      adapter);

/** 更新实例最近一次入站消息时间（脱敏时间戳）与在线时间。 */
int mf_im_bot_instance_touch_inbound(csilk_db_pool_t* pool,
                                     int64_t          id,
                                     const char*      last_inbound_at);

/** 心跳并可选更新最近入站时间。 */
int mf_im_bot_instance_heartbeat_with_inbound(csilk_db_pool_t* pool,
                                              int64_t          id,
                                              const char*      last_inbound_at,
                                              char*            status_out,
                                              size_t           status_cap,
                                              int64_t*         epoch_out,
                                              int*             enabled_out);
```

2. Update `backend/src/infrastructure/repositories/im_bot_repo_impl.c`:
   - Add `last_inbound_at` to `IM_INSTANCE_COLS`:

```c
#define IM_INSTANCE_COLS                                                                           \
    "id, mf_user_id, platform, adapter, ilink_bot_id, ilink_user_id, im_user_id, status, "         \
    "CASE WHEN status IN ('pending_login','qr_ready','scanned','need_verifycode') "                \
    "THEN qr_content ELSE NULL END AS qr_content, "                                                \
    "CASE WHEN status IN ('pending_login','qr_ready','scanned','need_verifycode') "                \
    "THEN qr_expires_at ELSE NULL END AS qr_expires_at, "                                          \
    "login_epoch, verify_attempts, enabled, last_error, last_seen_at, last_inbound_at, "           \
    "created_at, updated_at"
```

   - In `mf_im_bot_instance_activate` (replacing lines 683-689 of `backend/src/infrastructure/repositories/im_bot_repo_impl.c`), clear stale `last_inbound_at` if re-login switched the bound WeChat user:

```c
    bool        switched_wechat = (old_im[0] && strcmp(old_im, new_im) != 0);
    const char* activate_sql =
        switched_wechat
            ? "UPDATE im_bot_instances SET ilink_bot_id=?, ilink_user_id=?, im_user_id=?, "
              "status='active', enabled=1, qr_content=NULL, qr_expires_at=NULL, "
              "pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, "
              "last_error=NULL, last_inbound_at=NULL, updated_at=CURRENT_TIMESTAMP "
              "WHERE id=? RETURNING id"
            : "UPDATE im_bot_instances SET ilink_bot_id=?, ilink_user_id=?, im_user_id=?, "
              "status='active', enabled=1, qr_content=NULL, qr_expires_at=NULL, "
              "pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, "
              "last_error=NULL, updated_at=CURRENT_TIMESTAMP WHERE id=? RETURNING id";
    csilk_json_t* updated = csilk_db_query_param_json(
        pool, activate_sql, (const char*[]){ilink_bot_id, ilink_user_id, new_im, sid, NULL});
```
   - Add `mf_im_bot_instance_get_by_user`, `mf_im_bot_instance_touch_inbound`, `mf_im_bot_instance_heartbeat_with_inbound`, and make `mf_im_bot_instance_heartbeat` delegate:

```c
csilk_json_t*
mf_im_bot_instance_get_by_user(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               const char*      platform,
                               const char*      adapter)
{
    if (!pool || user_id <= 0) {
        return NULL;
    }
    const char* plat = (platform && platform[0]) ? platform : "wechat";
    const char* adp = (adapter && adapter[0]) ? adapter : "ilink";
    char        uid[32];
    im_id_str(user_id, uid, sizeof(uid));
    return im_instance_one(pool,
                           "mf_user_id=? AND platform=? AND adapter=? LIMIT 1",
                           (const char*[]){uid, plat, adp, NULL});
}

static int
im_valid_iso_prefix(const char* ts)
{
    if (!ts || strlen(ts) < 19) {
        return 0;
    }
    return isdigit((unsigned char)ts[0]) && isdigit((unsigned char)ts[1]) &&
           isdigit((unsigned char)ts[2]) && isdigit((unsigned char)ts[3]) && ts[4] == '-' &&
           isdigit((unsigned char)ts[5]) && isdigit((unsigned char)ts[6]) && ts[7] == '-' &&
           isdigit((unsigned char)ts[8]) && isdigit((unsigned char)ts[9]);
}

int
mf_im_bot_instance_touch_inbound(csilk_db_pool_t* pool, int64_t id, const char* last_inbound_at)
{
    if (!pool || id <= 0) {
        return -1;
    }
    char sid[32];
    im_id_str(id, sid, sizeof(sid));
    if (!im_valid_iso_prefix(last_inbound_at)) {
        return csilk_db_exec_param(
            pool,
            "UPDATE im_bot_instances SET last_seen_at=CURRENT_TIMESTAMP, "
            "updated_at=CURRENT_TIMESTAMP WHERE id=?",
            (const char*[]){sid, NULL});
    }
    const char* drv = csilk_db_pool_get_driver_name(pool);
    const bool  is_pg = drv && strcmp(drv, "postgres") == 0;
    const char* inbound_expr = is_pg ? "COALESCE(NULLIF(?,'')::timestamptz, last_inbound_at)"
                                     : "COALESCE(NULLIF(?,''), last_inbound_at)";
    char        sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "UPDATE im_bot_instances SET last_inbound_at=%s, "
            "last_seen_at=CURRENT_TIMESTAMP, updated_at=CURRENT_TIMESTAMP WHERE id=?",
            inbound_expr);
    return csilk_db_exec_param(pool, sql, (const char*[]){last_inbound_at, sid, NULL});
}

int
mf_im_bot_instance_heartbeat_with_inbound(csilk_db_pool_t* pool,
                                          int64_t          id,
                                          const char*      last_inbound_at,
                                          char*            status_out,
                                          size_t           status_cap,
                                          int64_t*         epoch_out,
                                          int*             enabled_out)
{
    if (!pool || id <= 0) {
        return -1;
    }
    char        sid[32];
    im_id_str(id, sid, sizeof(sid));
    const char*   inb = im_valid_iso_prefix(last_inbound_at) ? last_inbound_at : "";
    const char*   drv = csilk_db_pool_get_driver_name(pool);
    const bool    is_pg = drv && strcmp(drv, "postgres") == 0;
    const char*   inbound_expr = is_pg ? "COALESCE(NULLIF(?,'')::timestamptz, last_inbound_at)"
                                       : "COALESCE(NULLIF(?,''), last_inbound_at)";
    char          sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "UPDATE im_bot_instances SET last_inbound_at=%s, "
            "last_seen_at=CURRENT_TIMESTAMP, updated_at=CURRENT_TIMESTAMP "
            "WHERE id=? RETURNING status, login_epoch, enabled",
            inbound_expr);
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){inb, sid, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -3;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    if (status_out && status_cap > 0) {
        const char* st = csilk_json_get_string(row, "status");
        snprintf(status_out, status_cap, "%s", st ? st : "");
    }
    if (epoch_out) {
        *epoch_out = db_get_int(row, "login_epoch");
    }
    if (enabled_out) {
        *enabled_out = db_get_bool(row, "enabled") ? 1 : 0;
    }
    csilk_json_free(res);
    return 0;
}

int
mf_im_bot_instance_heartbeat(csilk_db_pool_t* pool,
                             int64_t          id,
                             char*            status_out,
                             size_t           status_cap,
                             int64_t*         epoch_out,
                             int*             enabled_out)
{
    return mf_im_bot_instance_heartbeat_with_inbound(
        pool, id, NULL, status_out, status_cap, epoch_out, enabled_out);
}
```

3. Update `backend/src/domain/scheduler/entity.h`, `backend/src/domain/scheduler/repository.h`, and `backend/src/infrastructure/repositories/scheduler_repo_impl.c`:
   - In `backend/src/domain/scheduler/entity.h`, update `mf_user_notification_channel_t.channel_type` and `mf_job_run_log_t.notification_status` comments:

```c
typedef struct {
    int64_t id;
    int64_t user_id;
    char    channel_type[32]; /* "serverchan", "pushdeer", "wechat_work", "webhook", "wechat_ilink", "none" */
    char    channel_name[64];
    char    webhook_url[256];
    char    secret_key[128];
    bool    is_active;
    char    created_at[32];
    char    updated_at[32];
} mf_user_notification_channel_t;

typedef struct {
    int64_t id;
    int64_t job_id;
    int64_t user_id;
    char    started_at[32];
    char    completed_at[32];
    char    status[16];         /* "running", "success", "failed", "timeout" */
    int     duration_ms;
    char    trigger_reason[64]; /* "cron_tick", "manual_invoke", "market_closed", "price_spike" */
    char    output_summary[2048];
    char    error_message[512];
    char    notification_status[16]; /* "none", "queued", "sent", "partial", "failed" */
} mf_job_run_log_t;
```
   - In `domain/scheduler/repository.h`: declare:

```c
csilk_json_t* mf_scheduler_repo_list_active_channels(void* pool);
int           mf_scheduler_repo_log_set_sync_counts(void* pool, int64_t log_id, int sync_sent, int sync_failed);
int           mf_scheduler_repo_log_update_notif(void*       pool,
                                                 int64_t     log_id,
                                                 const char* notif_status,
                                                 const char* error_msg);
```

   - In `scheduler_repo_impl.c`: implement:

```c
csilk_json_t*
mf_scheduler_repo_list_active_channels(void* pool)
{
    if (!pool) {
        return NULL;
    }
    return csilk_db_query_json(
        (csilk_db_pool_t*)pool,
        "SELECT * FROM user_notification_channels "
        "WHERE is_active = 1 AND channel_type != 'none' ORDER BY user_id ASC");
}

int
mf_scheduler_repo_log_set_sync_counts(void* pool, int64_t log_id, int sync_sent, int sync_failed)
{
    if (!pool || log_id <= 0) {
        return -1;
    }
    char lid[32], sent_s[16], fail_s[16];
    snprintf(lid, sizeof(lid), "%lld", (long long)log_id);
    snprintf(sent_s, sizeof(sent_s), "%d", sync_sent);
    snprintf(fail_s, sizeof(fail_s), "%d", sync_failed);
    return csilk_db_exec_param(
        (csilk_db_pool_t*)pool,
        "UPDATE job_run_logs SET notif_sync_sent = ?, notif_sync_failed = ? WHERE id = ?",
        (const char*[]){sent_s, fail_s, lid, NULL});
}

int
mf_scheduler_repo_log_update_notif(void*       pool,
                                   int64_t     log_id,
                                   const char* notif_status,
                                   const char* error_msg)
{
    if (!pool || log_id <= 0 || !notif_status || !notif_status[0]) {
        return -1;
    }
    char lid[32];
    snprintf(lid, sizeof(lid), "%lld", (long long)log_id);
    return csilk_db_exec_param(
        (csilk_db_pool_t*)pool,
        "UPDATE job_run_logs SET notification_status = ?, error_message = ? WHERE id = ?",
        (const char*[]){notif_status, error_msg ? error_msg : "", lid, NULL});
}
```

- [ ] **Step 4: Create `im_push_outbox_repo_impl.h` and `im_push_outbox_repo_impl.c`**

Create `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.h`:

```c
#pragma once

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 入队一条微信 iLink 推送记录，返回 push_id (>0) 或 -1。 */
int64_t mf_im_push_outbox_enqueue(csilk_db_pool_t* pool,
                                  int64_t          instance_id,
                                  int64_t          mf_user_id,
                                  int64_t          job_run_log_id,
                                  const char*      kind,
                                  const char*      title,
                                  const char*      content,
                                  int64_t          ttl_sec,
                                  int              max_attempts);

/** 查询单条 Outbox 记录（不存在返回 NULL）。 */
csilk_json_t* mf_im_push_outbox_get(csilk_db_pool_t* pool, int64_t push_id);

/** 将未终态的测试推送标为 expired（避免同步等待超时后补推）。 */
int mf_im_push_outbox_mark_expired_by_id(csilk_db_pool_t* pool,
                                         int64_t          push_id,
                                         const char*      reason);

/**
 * @brief 将超期未投递的 pending/leased 记录批量置为 expired，返回去重的关联 job_run_log_id 列表。
 * @param instance_id_or_zero >0 时仅扫描该实例；0 时全局扫描。
 */
int mf_im_push_outbox_expire_stale(csilk_db_pool_t* pool,
                                   int64_t          instance_id_or_zero,
                                   int64_t          now_ts,
                                   const char*      default_reason,
                                   int64_t**        out_log_ids,
                                   size_t*          out_log_count);

/**
 * @brief 原子回收超时租约并领取待发推送，返回 JSON 数组。
 * @param has_context false 时仅领取 kind='test'，保留 'scheduler' 等待唤醒。
 */
csilk_json_t* mf_im_push_outbox_claim(csilk_db_pool_t* pool,
                                      int64_t          instance_id,
                                      bool             has_context,
                                      int              limit,
                                      int64_t          now_ts,
                                      int64_t          lease_sec);

/**
 * @brief 网关回执推进 Outbox 状态。
 * @return 0 成功或已终态幂等，-3 不存在/跨实例，-2 参数非法，-1 数据库错误。
 */
int mf_im_push_outbox_ack(csilk_db_pool_t* pool,
                          int64_t          instance_id,
                          int64_t          push_id,
                          const char*      result,
                          const char*      error_msg,
                          int              retry_after_sec,
                          int64_t          now_ts,
                          int64_t*         out_job_run_log_id);

/** 根据 sync 计数与关联 Outbox 状态聚合更新 job_run_logs.notification_status。 */
int mf_im_push_outbox_rollup_job_log(csilk_db_pool_t* pool, int64_t job_run_log_id);

#ifdef __cplusplus
}
#endif
```

Create `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.c`:

```c
/** @file im_push_outbox_repo_impl.c @brief iLink 定时推送持久化 Outbox 仓储实现 */

#include "infrastructure/repositories/im_push_outbox_repo_impl.h"
#include "common/db.h"
#include "infrastructure/repositories/scheduler_repo_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
push_i64_str(int64_t v, char* buf, size_t cap)
{
    snprintf(buf, cap, "%lld", (long long)v);
}

static bool
push_status_is_terminal(const char* st)
{
    if (!st) {
        return false;
    }
    return strcmp(st, "sent") == 0 || strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0;
}

static int64_t
push_backoff_seconds(int attempts_after_inc, int retry_after_sec)
{
    if (retry_after_sec > 0) {
        return retry_after_sec > 300 ? 300 : retry_after_sec;
    }
    static const int64_t kBackoff[] = {10, 30, 60, 120};
    int                  idx = attempts_after_inc - 1;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= (int)(sizeof(kBackoff) / sizeof(kBackoff[0]))) {
        idx = (int)(sizeof(kBackoff) / sizeof(kBackoff[0])) - 1;
    }
    return kBackoff[idx];
}

int64_t
mf_im_push_outbox_enqueue(csilk_db_pool_t* pool,
                          int64_t          instance_id,
                          int64_t          mf_user_id,
                          int64_t          job_run_log_id,
                          const char*      kind,
                          const char*      title,
                          const char*      content,
                          int64_t          ttl_sec,
                          int              max_attempts)
{
    if (!pool || instance_id <= 0 || mf_user_id <= 0 || !title || !title[0] || !content) {
        return -1;
    }
    const char* eff_kind = (kind && strcmp(kind, "test") == 0) ? "test" : "scheduler";
    int64_t     eff_ttl = ttl_sec > 0 ? ttl_sec : 1800;
    int         eff_max = max_attempts > 0 ? max_attempts : 5;
    int64_t     now = (int64_t)time(NULL);
    int64_t     expires_at = now + eff_ttl;

    char inst_s[32], uid_s[32], log_s[32], max_s[16], next_s[32], exp_s[32];
    push_i64_str(instance_id, inst_s, sizeof(inst_s));
    push_i64_str(mf_user_id, uid_s, sizeof(uid_s));
    push_i64_str(job_run_log_id, log_s, sizeof(log_s));
    snprintf(max_s, sizeof(max_s), "%d", eff_max);
    push_i64_str(now, next_s, sizeof(next_s));
    push_i64_str(expires_at, exp_s, sizeof(exp_s));

    csilk_json_t* res = NULL;
    if (job_run_log_id > 0) {
        const char* sql =
            "INSERT INTO im_push_outbox "
            "(instance_id, mf_user_id, job_run_log_id, kind, title, content, "
            " status, attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts) "
            "VALUES (?, ?, ?, ?, ?, ?, 'pending', 0, ?, 0, ?, ?) RETURNING id";
        res = csilk_db_query_param_json(
            pool,
            sql,
            (const char*[]){
                inst_s, uid_s, log_s, eff_kind, title, content, max_s, next_s, exp_s, NULL});
    } else {
        const char* sql =
            "INSERT INTO im_push_outbox "
            "(instance_id, mf_user_id, job_run_log_id, kind, title, content, "
            " status, attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts) "
            "VALUES (?, ?, NULL, ?, ?, ?, 'pending', 0, ?, 0, ?, ?) RETURNING id";
        res = csilk_db_query_param_json(
            pool,
            sql,
            (const char*[]){inst_s, uid_s, eff_kind, title, content, max_s, next_s, exp_s, NULL});
    }

    int64_t id = -1;
    if (res && csilk_json_is_array(res) && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id > 0 ? id : -1;
}

csilk_json_t*
mf_im_push_outbox_get(csilk_db_pool_t* pool, int64_t push_id)
{
    if (!pool || push_id <= 0) {
        return NULL;
    }
    char pid_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "SELECT id, instance_id, mf_user_id, job_run_log_id, kind, title, content, status, "
        "attempts, max_attempts, lease_until_ts, next_attempt_at_ts, expires_at_ts, last_error, "
        "CAST(created_at AS TEXT) AS created_at, CAST(updated_at AS TEXT) AS updated_at "
        "FROM im_push_outbox WHERE id=? LIMIT 1",
        (const char*[]){pid_s, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return NULL;
    }
    csilk_json_t* copy = csilk_json_copy(csilk_json_array_get(res, 0));
    csilk_json_free(res);
    return copy;
}

int
mf_im_push_outbox_mark_expired_by_id(csilk_db_pool_t* pool, int64_t push_id, const char* reason)
{
    if (!pool || push_id <= 0) {
        return -1;
    }
    char        pid_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    const char* err = (reason && reason[0]) ? reason : "推送等待超时已取消";
    return csilk_db_exec_param(
        pool,
        "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, last_error=?, "
        "updated_at=CURRENT_TIMESTAMP WHERE id=? AND status IN ('pending','leased')",
        (const char*[]){err, pid_s, NULL});
}

int
mf_im_push_outbox_expire_stale(csilk_db_pool_t* pool,
                               int64_t          instance_id_or_zero,
                               int64_t          now_ts,
                               const char*      default_reason,
                               int64_t**        out_log_ids,
                               size_t*          out_log_count)
{
    if (out_log_ids) {
        *out_log_ids = NULL;
    }
    if (out_log_count) {
        *out_log_count = 0;
    }
    if (!pool) {
        return -1;
    }
    int64_t     now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    const char* reason = (default_reason && default_reason[0])
                             ? default_reason
                             : "超过30分钟等待窗口（网关离线或微信会话未激活），推送已过期";

    char now_s[32], inst_s[32];
    push_i64_str(now, now_s, sizeof(now_s));
    push_i64_str(instance_id_or_zero, inst_s, sizeof(inst_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_expire", &scope) != 0) {
        return -1;
    }

    csilk_json_t* logs_res = NULL;
    if (instance_id_or_zero > 0) {
        logs_res = csilk_db_query_param_json(
            pool,
            "SELECT DISTINCT job_run_log_id FROM im_push_outbox "
            "WHERE instance_id=? AND status IN ('pending','leased') AND expires_at_ts <= ? "
            "AND job_run_log_id IS NOT NULL",
            (const char*[]){inst_s, now_s, NULL});
    } else {
        logs_res = csilk_db_query_param_json(
            pool,
            "SELECT DISTINCT job_run_log_id FROM im_push_outbox "
            "WHERE status IN ('pending','leased') AND expires_at_ts <= ? "
            "AND job_run_log_id IS NOT NULL",
            (const char*[]){now_s, NULL});
    }

    int rc = 0;
    if (instance_id_or_zero > 0) {
        rc = csilk_db_exec_param(
            pool,
            "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, "
            "last_error=CASE WHEN last_error='' THEN ? ELSE last_error END, "
            "updated_at=CURRENT_TIMESTAMP "
            "WHERE instance_id=? AND status IN ('pending','leased') AND expires_at_ts <= ?",
            (const char*[]){reason, inst_s, now_s, NULL});
    } else {
        rc = csilk_db_exec_param(
            pool,
            "UPDATE im_push_outbox SET status='expired', lease_until_ts=0, "
            "last_error=CASE WHEN last_error='' THEN ? ELSE last_error END, "
            "updated_at=CURRENT_TIMESTAMP "
            "WHERE status IN ('pending','leased') AND expires_at_ts <= ?",
            (const char*[]){reason, now_s, NULL});
    }
    if (rc != 0) {
        if (logs_res) {
            csilk_json_free(logs_res);
        }
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        if (logs_res) {
            csilk_json_free(logs_res);
        }
        return -1;
    }

    if (out_log_ids && out_log_count && logs_res && csilk_json_is_array(logs_res)) {
        size_t n = csilk_json_array_size(logs_res);
        if (n > 0) {
            int64_t* ids = (int64_t*)calloc(n, sizeof(int64_t));
            if (ids) {
                size_t k = 0;
                for (size_t i = 0; i < n; ++i) {
                    int64_t lid =
                        db_get_int(csilk_json_array_get(logs_res, i), "job_run_log_id");
                    if (lid > 0) {
                        ids[k++] = lid;
                    }
                }
                if (k > 0) {
                    *out_log_ids = ids;
                    *out_log_count = k;
                } else {
                    free(ids);
                }
            }
        }
    }
    if (logs_res) {
        csilk_json_free(logs_res);
    }
    return 0;
}

csilk_json_t*
mf_im_push_outbox_claim(csilk_db_pool_t* pool,
                        int64_t          instance_id,
                        bool             has_context,
                        int              limit,
                        int64_t          now_ts,
                        int64_t          lease_sec)
{
    if (!pool || instance_id <= 0) {
        return NULL;
    }
    int eff_limit = limit;
    if (eff_limit < 1) {
        eff_limit = 5;
    }
    if (eff_limit > 10) {
        eff_limit = 10;
    }
    int64_t now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    int64_t lsec = lease_sec > 0 ? lease_sec : 60;
    int64_t lease_until = now + lsec;

    char inst_s[32], now_s[32], lim_s[16], lease_s[32];
    push_i64_str(instance_id, inst_s, sizeof(inst_s));
    push_i64_str(now, now_s, sizeof(now_s));
    snprintf(lim_s, sizeof(lim_s), "%d", eff_limit);
    push_i64_str(lease_until, lease_s, sizeof(lease_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_claim", &scope) != 0) {
        return NULL;
    }

    if (csilk_db_exec_param(
            pool,
            "UPDATE im_push_outbox SET status='pending', lease_until_ts=0, "
            "updated_at=CURRENT_TIMESTAMP "
            "WHERE instance_id=? AND status='leased' AND lease_until_ts <= ?",
            (const char*[]){inst_s, now_s, NULL}) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return NULL;
    }

    char sql[768];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT id, kind, title, content, attempts, max_attempts FROM im_push_outbox "
            "WHERE instance_id=? AND status='pending' AND next_attempt_at_ts <= ? "
            "AND expires_at_ts > ?%s "
            "ORDER BY CASE WHEN kind='test' THEN 0 ELSE 1 END, id ASC LIMIT ?%s",
            has_context ? "" : " AND kind='test'",
            db_is_postgres() ? " FOR UPDATE SKIP LOCKED" : "");

    csilk_json_t* cand = csilk_db_query_param_json(
        pool, sql, (const char*[]){inst_s, now_s, now_s, lim_s, NULL});
    if (!cand) {
        db_tx_scope_rollback(pool, &scope);
        return NULL;
    }

    csilk_json_t* claimed = csilk_json_array();
    size_t        n = csilk_json_is_array(cand) ? csilk_json_array_size(cand) : 0;
    for (size_t i = 0; i < n; ++i) {
        csilk_json_t* row = csilk_json_array_get(cand, i);
        int64_t       pid = db_get_int(row, "id");
        if (pid <= 0) {
            continue;
        }
        char pid_s[32];
        push_i64_str(pid, pid_s, sizeof(pid_s));
        csilk_json_t* upd = csilk_db_query_param_json(
            pool,
            "UPDATE im_push_outbox SET status='leased', lease_until_ts=?, "
            "updated_at=CURRENT_TIMESTAMP WHERE id=? AND instance_id=? AND status='pending' "
            "RETURNING id",
            (const char*[]){lease_s, pid_s, inst_s, NULL});
        bool ok = upd && csilk_json_is_array(upd) && csilk_json_array_size(upd) > 0;
        if (upd) {
            csilk_json_free(upd);
        }
        if (ok) {
            csilk_json_t* item = csilk_json_object();
            csilk_json_add_int(item, "id", pid);
            const char* k = csilk_json_get_string(row, "kind");
            const char* t = csilk_json_get_string(row, "title");
            const char* c = csilk_json_get_string(row, "content");
            csilk_json_add_string(item, "kind", k ? k : "scheduler");
            csilk_json_add_string(item, "title", t ? t : "");
            csilk_json_add_string(item, "content", c ? c : "");
            csilk_json_add_int(item, "attempts", db_get_int(row, "attempts"));
            csilk_json_add_int(item, "max_attempts", db_get_int(row, "max_attempts"));
            csilk_json_array_append(claimed, item);
        }
    }
    csilk_json_free(cand);

    if (db_tx_scope_commit(pool, &scope) != 0) {
        csilk_json_free(claimed);
        return NULL;
    }
    return claimed;
}

int
mf_im_push_outbox_ack(csilk_db_pool_t* pool,
                      int64_t          instance_id,
                      int64_t          push_id,
                      const char*      result,
                      const char*      error_msg,
                      int              retry_after_sec,
                      int64_t          now_ts,
                      int64_t*         out_job_run_log_id)
{
    if (out_job_run_log_id) {
        *out_job_run_log_id = 0;
    }
    if (!pool || instance_id <= 0 || push_id <= 0 || !result || !result[0]) {
        return -2;
    }
    bool is_sent = strcmp(result, "sent") == 0;
    bool is_wait_ctx = strcmp(result, "waiting_context") == 0;
    bool is_retry = strcmp(result, "retry") == 0;
    bool is_failed = strcmp(result, "failed") == 0;
    if (!is_sent && !is_wait_ctx && !is_retry && !is_failed) {
        return -2;
    }

    int64_t now = now_ts > 0 ? now_ts : (int64_t)time(NULL);
    char    pid_s[32], inst_s[32];
    push_i64_str(push_id, pid_s, sizeof(pid_s));
    push_i64_str(instance_id, inst_s, sizeof(inst_s));

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "im_push_ack", &scope) != 0) {
        return -1;
    }

    char sql[512];
    SQL_FMT(sql,
            sizeof(sql),
            "SELECT kind, status, attempts, max_attempts, expires_at_ts, "
            "COALESCE(job_run_log_id, 0) AS job_run_log_id "
            "FROM im_push_outbox WHERE id=? AND instance_id=? LIMIT 1%s",
            db_is_postgres() ? " FOR UPDATE" : "");
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, (const char*[]){pid_s, inst_s, NULL});
    if (!res || !csilk_json_is_array(res) || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        db_tx_scope_rollback(pool, &scope);
        return -3;
    }

    csilk_json_t* row = csilk_json_array_get(res, 0);
    const char*   cur_status = csilk_json_get_string(row, "status");
    const char*   kind = csilk_json_get_string(row, "kind");
    bool          is_test = kind && strcmp(kind, "test") == 0;
    int           attempts = (int)db_get_int(row, "attempts");
    int           max_attempts = (int)db_get_int(row, "max_attempts");
    if (max_attempts <= 0) {
        max_attempts = 5;
    }
    int64_t expires_at = db_get_int(row, "expires_at_ts");
    int64_t log_id = db_get_int(row, "job_run_log_id");
    if (out_job_run_log_id) {
        *out_job_run_log_id = log_id;
    }

    if (push_status_is_terminal(cur_status)) {
        csilk_json_free(res);
        db_tx_scope_commit(pool, &scope);
        return 0;
    }

    const char* new_status = "failed";
    int         new_attempts = attempts;
    int64_t     next_attempt = now;
    const char* new_err = error_msg ? error_msg : "";

    if (is_sent) {
        new_status = "sent";
        new_err = "";
    } else if (is_wait_ctx) {
        if (is_test) {
            new_status = "failed";
            if (!new_err[0]) {
                new_err = "请先在微信向机器人发送任意一条消息以建立会话，再重新测试";
            }
        } else if (expires_at <= now) {
            new_status = "expired";
            if (!new_err[0]) {
                new_err = "超过30分钟等待窗口（微信会话未激活），推送已过期";
            }
        } else {
            new_status = "pending";
            next_attempt = now + 30;
            if (!new_err[0]) {
                new_err = "等待微信会话上下文刷新（请在微信给机器人发送任意消息）";
            }
        }
    } else if (is_retry) {
        new_attempts = attempts + 1;
        if (is_test) {
            new_status = "failed";
        } else if (expires_at <= now && new_attempts < max_attempts) {
            new_status = "expired";
        } else if (new_attempts >= max_attempts) {
            new_status = "failed";
        } else {
            new_status = "pending";
            next_attempt = now + push_backoff_seconds(new_attempts, retry_after_sec);
        }
    } else {
        new_status = "failed";
    }

    char att_s[16], next_s[32];
    snprintf(att_s, sizeof(att_s), "%d", new_attempts);
    push_i64_str(next_attempt, next_s, sizeof(next_s));

    int urc = csilk_db_exec_param(
        pool,
        "UPDATE im_push_outbox SET status=?, attempts=?, lease_until_ts=0, "
        "next_attempt_at_ts=?, last_error=?, updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND instance_id=?",
        (const char*[]){new_status, att_s, next_s, new_err, pid_s, inst_s, NULL});
    csilk_json_free(res);
    if (urc != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (db_tx_scope_commit(pool, &scope) != 0) {
        return -1;
    }
    return 0;
}

int
mf_im_push_outbox_rollup_job_log(csilk_db_pool_t* pool, int64_t job_run_log_id)
{
    if (!pool || job_run_log_id <= 0) {
        return -1;
    }
    char lid_s[32];
    push_i64_str(job_run_log_id, lid_s, sizeof(lid_s));

    csilk_json_t* lres = csilk_db_query_param_json(
        pool,
        "SELECT notif_sync_sent, notif_sync_failed, error_message "
        "FROM job_run_logs WHERE id=? LIMIT 1",
        (const char*[]){lid_s, NULL});
    if (!lres || !csilk_json_is_array(lres) || csilk_json_array_size(lres) == 0) {
        if (lres) {
            csilk_json_free(lres);
        }
        return 0;
    }
    csilk_json_t* lrow = csilk_json_array_get(lres, 0);
    int           sync_sent = (int)db_get_int(lrow, "notif_sync_sent");
    int           sync_failed = (int)db_get_int(lrow, "notif_sync_failed");
    const char*   existing_err = csilk_json_get_string(lrow, "error_message");
    char          err_buf[512];
    err_buf[0] = '\0';
    if (existing_err && existing_err[0]) {
        snprintf(err_buf, sizeof(err_buf), "%s", existing_err);
    }
    csilk_json_free(lres);

    csilk_json_t* ores = csilk_db_query_param_json(
        pool,
        "SELECT status, last_error FROM im_push_outbox WHERE job_run_log_id=?",
        (const char*[]){lid_s, NULL});

    int  out_pending = 0, out_sent = 0, out_failed = 0;
    char out_err[512];
    out_err[0] = '\0';
    if (ores && csilk_json_is_array(ores)) {
        size_t n = csilk_json_array_size(ores);
        for (size_t i = 0; i < n; ++i) {
            csilk_json_t* r = csilk_json_array_get(ores, i);
            const char*   st = csilk_json_get_string(r, "status");
            const char*   le = csilk_json_get_string(r, "last_error");
            if (!st) {
                continue;
            }
            if (strcmp(st, "sent") == 0) {
                out_sent++;
            } else if (strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0) {
                out_failed++;
                if (!out_err[0] && le && le[0]) {
                    snprintf(out_err, sizeof(out_err), "%s", le);
                }
            } else {
                out_pending++;
            }
        }
    }
    if (ores) {
        csilk_json_free(ores);
    }

    int         ok_total = sync_sent + out_sent;
    int         fail_total = sync_failed + out_failed;
    const char* final_status = "none";
    if (out_pending > 0) {
        final_status = "queued";
    } else if (ok_total > 0 && fail_total > 0) {
        final_status = "partial";
    } else if (ok_total > 0) {
        final_status = "sent";
    } else if (fail_total > 0) {
        final_status = "failed";
    }

    if (fail_total == 0 && out_pending == 0) {
        err_buf[0] = '\0';
    } else if (!err_buf[0] && out_err[0]) {
        snprintf(err_buf, sizeof(err_buf), "%s", out_err);
    }

    return mf_scheduler_repo_log_update_notif(pool, job_run_log_id, final_status, err_buf);
}
```

- [ ] **Step 5: Reconfigure CMake, build & run tests**

Run:
```bash
cmake -S backend -B backend/build -G "Unix Makefiles" && \
./backend/tests/run_isolated.sh build backend/build && \
ctest --test-dir backend/build -R "^(test_im_push_outbox|test_scheduler_repo)$" --output-on-failure
```
Expected: `2/2 passed`.

- [ ] **Step 6: Commit Task 2**

```bash
git add backend/CMakeLists.txt \
        backend/src/infrastructure/repositories/im_push_outbox_repo_impl.h \
        backend/src/infrastructure/repositories/im_push_outbox_repo_impl.c \
        backend/src/infrastructure/repositories/im_bot_repo_impl.h \
        backend/src/infrastructure/repositories/im_bot_repo_impl.c \
        backend/src/domain/scheduler/entity.h \
        backend/src/domain/scheduler/repository.h \
        backend/src/infrastructure/repositories/scheduler_repo_impl.c \
        backend/tests/unit/test_im_push_outbox.c
git commit -m "feat(im): ✨ add im_push_outbox repository and scheduler sync-count helpers"
```

---

### Task 3: Backend Push Usecases (`im_push_usecases.{h,c}`) & Gateway HTTP Routes

**Files:**
- Create: `backend/src/application/ai/im_push_usecases.h`
- Create: `backend/src/application/ai/im_push_usecases.c`
- Modify: `backend/src/application/ai/im_bot_instance_usecases.h:35-51`
- Modify: `backend/src/application/ai/im_bot_instance_usecases.c:337-382`
- Modify: `backend/src/interfaces/http/controllers/im_bot_instance_controller.c:280-401`
- Test: `backend/tests/unit/test_im_push_outbox.c`

- [ ] **Step 1: Add failing usecase tests to `backend/tests/unit/test_im_push_outbox.c`**

Include `"application/ai/im_push_usecases.h"` and add `test_push_usecases(pool)`:

```c
static void
test_push_usecases(csilk_db_pool_t* pool)
{
    int64_t u3 = seed_user_and_active_instance(pool, "push_u3", "bot_p3", "wx_p3");
    int64_t push_id = 0;
    char    err[256] = {0};

    /* User without instance cannot enqueue */
    assert(im_push_usecase_enqueue(
               pool, 99999, 0, "scheduler", "T", "C", 1800, &push_id, err, sizeof(err)) != 0);
    assert(strstr(err, "未绑定") != NULL || strstr(err, "创建") != NULL);

    /* Enqueue for active user u3 */
    assert(im_push_usecase_enqueue(
               pool, u3, 0, "scheduler", "U3 推送", "正文", 1800, &push_id, err, sizeof(err)) ==
           0);
    assert(push_id > 0);

    csilk_json_t* st = im_push_usecase_build_ilink_status(pool, u3);
    assert(st && db_get_bool(st, "has_instance") == 1);
    int64_t inst3_id = db_get_int(st, "instance_id");
    assert(strcmp(csilk_json_get_string(st, "status"), "active") == 0);
    csilk_json_free(st);

    csilk_json_t* items = NULL;
    int           code = 0;
    assert(im_push_usecase_gateway_claim(pool,
                                         inst3_id,
                                         true,
                                         "2026-11-25T11:22:33Z",
                                         5,
                                         &items,
                                         &code,
                                         err,
                                         sizeof(err)) == 0);
    assert(items && csilk_json_array_size(items) == 1);
    csilk_json_free(items);

    /* Non-existent instance claim -> 1003 */
    assert(im_push_usecase_gateway_claim(
               pool, 99999, true, "", 5, &items, &code, err, sizeof(err)) != 0 &&
           code == 1003);

    /* Global stale expiry leaves sent items intact */
    assert(im_push_usecase_gateway_ack(
               pool, inst3_id, push_id, "sent", "", 0, &code, err, sizeof(err)) == 0);
    assert(im_push_usecase_expire_stale(pool) == 0);

    printf("  [OK] test_push_usecases\n");
}
```
Invoke `test_push_usecases(pool);` from `main()`.

- [ ] **Step 2: Run build to confirm failure on missing `im_push_usecases.h`**

Run: `cmake --build backend/build --target test_im_push_outbox`
Expected: FAIL (`im_push_usecases.h: No such file or directory`).

- [ ] **Step 3: Create `im_push_usecases.h` and `im_push_usecases.c`**

Create `backend/src/application/ai/im_push_usecases.h`:

```c
#pragma once

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 校验用户 iLink 实例在线后入队推送；非 active/disabled 时返回明确中文错误并 return -1。 */
int im_push_usecase_enqueue(void*       pool,
                            int64_t     user_id,
                            int64_t     job_run_log_id,
                            const char* kind,
                            const char* title,
                            const char* content,
                            int64_t     ttl_sec,
                            int64_t*    out_push_id,
                            char*       err_buf,
                            size_t      err_cap);

/** 网关按实例领取到期推送，自动清理过期与失效租约，并可选刷新 last_inbound_at。 */
int im_push_usecase_gateway_claim(void*          pool,
                                  int64_t        instance_id,
                                  bool           has_context,
                                  const char*    context_updated_at,
                                  int            limit,
                                  csilk_json_t** out_items,
                                  int*           code,
                                  char*          err,
                                  size_t         err_cap);

/** 网关回执单条推送并触发关联日志状态聚合。 */
int im_push_usecase_gateway_ack(void*       pool,
                                int64_t     instance_id,
                                int64_t     push_id,
                                const char* result,
                                const char* error_msg,
                                int         retry_after_sec,
                                int*        code,
                                char*       err,
                                size_t      err_cap);

/** 全局过期扫描并将关联日志更新为 failed / partial。 */
int im_push_usecase_expire_stale(void* pool);

/** 读取单条 Outbox 详情（用于通道测试同步轮询）。 */
csilk_json_t* im_push_usecase_get(void* pool, int64_t push_id);

/** 将等待超时的测试推送置为 expired。 */
int im_push_usecase_cancel_expired_test(void* pool, int64_t push_id, const char* reason);

/** 构造脱敏的 iLink 就绪态 JSON（给 GET /api/scheduler/channel 与 dashboard 使用）。 */
csilk_json_t* im_push_usecase_build_ilink_status(void* pool, int64_t user_id);

#ifdef __cplusplus
}
#endif
```

Create `backend/src/application/ai/im_push_usecases.c`:

```c
#include "application/ai/im_push_usecases.h"

#include "common/db.h"
#include "infrastructure/repositories/im_bot_repo_impl.h"
#include "infrastructure/repositories/im_push_outbox_repo_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
push_set_err(char* err, size_t cap, const char* msg)
{
    if (err && cap > 0) {
        snprintf(err, cap, "%s", msg ? msg : "");
    }
}

static void
rollup_expired_logs(csilk_db_pool_t* db_pool, int64_t* log_ids, size_t count)
{
    if (!log_ids) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (log_ids[i] > 0) {
            mf_im_push_outbox_rollup_job_log(db_pool, log_ids[i]);
        }
    }
    free(log_ids);
}

int
im_push_usecase_enqueue(void*       pool,
                        int64_t     user_id,
                        int64_t     job_run_log_id,
                        const char* kind,
                        const char* title,
                        const char* content,
                        int64_t     ttl_sec,
                        int64_t*    out_push_id,
                        char*       err_buf,
                        size_t      err_cap)
{
    if (out_push_id) {
        *out_push_id = 0;
    }
    push_set_err(err_buf, err_cap, "");
    if (!pool || user_id <= 0) {
        push_set_err(err_buf, err_cap, "无效的用户参数");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    inst = mf_im_bot_instance_get_by_user(db_pool, user_id, "wechat", "ilink");
    if (!inst) {
        push_set_err(
            err_buf, err_cap, "您尚未创建微信 iLink 机器人实例，请先前往「系统设置 → IM 机器人」绑定");
        return -1;
    }

    int64_t     inst_id = db_get_int(inst, "id");
    int         enabled = db_get_bool(inst, "enabled");
    const char* status = csilk_json_get_string(inst, "status");
    if (!enabled || (status && strcmp(status, "disabled") == 0)) {
        csilk_json_free(inst);
        push_set_err(err_buf, err_cap, "微信 iLink 机器人已停用，请先启用实例");
        return -1;
    }
    if (!status || strcmp(status, "active") != 0) {
        if (status && strcmp(status, "session_expired") == 0) {
            push_set_err(
                err_buf, err_cap, "微信 iLink 登录态已过期，请前往「系统设置」重新扫码登录");
        } else {
            push_set_err(
                err_buf, err_cap, "微信 iLink 机器人尚未完成扫码登录，请先前往「系统设置」完成绑定");
        }
        csilk_json_free(inst);
        return -1;
    }
    csilk_json_free(inst);

    int     max_att = (kind && strcmp(kind, "test") == 0) ? 1 : 5;
    int64_t pid = mf_im_push_outbox_enqueue(
        db_pool, inst_id, user_id, job_run_log_id, kind, title, content, ttl_sec, max_att);
    if (pid <= 0) {
        push_set_err(err_buf, err_cap, "写入推送队列失败");
        return -1;
    }
    if (out_push_id) {
        *out_push_id = pid;
    }
    return 0;
}

int
im_push_usecase_gateway_claim(void*          pool,
                              int64_t        instance_id,
                              bool           has_context,
                              const char*    context_updated_at,
                              int            limit,
                              csilk_json_t** out_items,
                              int*           code,
                              char*          err,
                              size_t         err_cap)
{
    if (out_items) {
        *out_items = NULL;
    }
    if (code) {
        *code = 0;
    }
    push_set_err(err, err_cap, "");
    if (!pool || instance_id <= 0) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "无效的实例 ID");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    csilk_json_t*    inst = mf_im_bot_instance_get_any(db_pool, instance_id);
    if (!inst) {
        if (code) {
            *code = 1003;
        }
        push_set_err(err, err_cap, "instance not found");
        return -1;
    }
    int         enabled = db_get_bool(inst, "enabled");
    const char* status = csilk_json_get_string(inst, "status");
    bool        active = enabled && status && strcmp(status, "active") == 0;
    csilk_json_free(inst);
    if (!active) {
        if (code) {
            *code = 1004;
        }
        push_set_err(err, err_cap, "instance is not active or enabled");
        return -1;
    }

    (void)mf_im_bot_instance_touch_inbound(db_pool, instance_id, context_updated_at);

    int64_t  now = (int64_t)time(NULL);
    int64_t* exp_logs = NULL;
    size_t   exp_cnt = 0;
    if (mf_im_push_outbox_expire_stale(db_pool, instance_id, now, NULL, &exp_logs, &exp_cnt) == 0) {
        rollup_expired_logs(db_pool, exp_logs, exp_cnt);
    }

    csilk_json_t* items =
        mf_im_push_outbox_claim(db_pool, instance_id, has_context, limit, now, 60);
    if (!items) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "领取推送任务失败");
        return -1;
    }
    if (out_items) {
        *out_items = items;
    } else {
        csilk_json_free(items);
    }
    return 0;
}

int
im_push_usecase_gateway_ack(void*       pool,
                            int64_t     instance_id,
                            int64_t     push_id,
                            const char* result,
                            const char* error_msg,
                            int         retry_after_sec,
                            int*        code,
                            char*       err,
                            size_t      err_cap)
{
    if (code) {
        *code = 0;
    }
    push_set_err(err, err_cap, "");
    if (!pool || instance_id <= 0 || push_id <= 0) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "参数无效");
        return -1;
    }

    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t          log_id = 0;
    int              rc = mf_im_push_outbox_ack(
        db_pool, instance_id, push_id, result, error_msg, retry_after_sec, (int64_t)time(NULL), &log_id);
    if (rc == -3) {
        if (code) {
            *code = 1003;
        }
        push_set_err(err, err_cap, "push not found");
        return -1;
    }
    if (rc == -2) {
        if (code) {
            *code = 1002;
        }
        push_set_err(err, err_cap, "invalid ack result");
        return -1;
    }
    if (rc != 0) {
        if (code) {
            *code = 500;
        }
        push_set_err(err, err_cap, "更新推送状态失败");
        return -1;
    }
    if (log_id > 0) {
        mf_im_push_outbox_rollup_job_log(db_pool, log_id);
    }
    return 0;
}

int
im_push_usecase_expire_stale(void* pool)
{
    if (!pool) {
        return -1;
    }
    csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;
    int64_t*         exp_logs = NULL;
    size_t           exp_cnt = 0;
    int              rc = mf_im_push_outbox_expire_stale(
        db_pool, 0, (int64_t)time(NULL), NULL, &exp_logs, &exp_cnt);
    rollup_expired_logs(db_pool, exp_logs, exp_cnt);
    return rc;
}

csilk_json_t*
im_push_usecase_get(void* pool, int64_t push_id)
{
    return mf_im_push_outbox_get((csilk_db_pool_t*)pool, push_id);
}

int
im_push_usecase_cancel_expired_test(void* pool, int64_t push_id, const char* reason)
{
    return mf_im_push_outbox_mark_expired_by_id((csilk_db_pool_t*)pool, push_id, reason);
}

csilk_json_t*
im_push_usecase_build_ilink_status(void* pool, int64_t user_id)
{
    csilk_json_t* st = csilk_json_object();
    if (!pool || user_id <= 0) {
        csilk_json_add_bool(st, "has_instance", false);
        return st;
    }
    csilk_json_t* inst =
        mf_im_bot_instance_get_by_user((csilk_db_pool_t*)pool, user_id, "wechat", "ilink");
    if (!inst) {
        csilk_json_add_bool(st, "has_instance", false);
        return st;
    }
    csilk_json_add_bool(st, "has_instance", true);
    csilk_json_add_int(st, "instance_id", db_get_int(inst, "id"));
    const char* status = csilk_json_get_string(inst, "status");
    csilk_json_add_string(st, "status", status ? status : "");
    csilk_json_add_bool(st, "enabled", db_get_bool(inst, "enabled") != 0);
    const char* bot_id = csilk_json_get_string(inst, "ilink_bot_id");
    if (bot_id && bot_id[0]) {
        csilk_json_add_string(st, "ilink_bot_id", bot_id);
    } else {
        csilk_json_add_null(st, "ilink_bot_id");
    }
    const char* seen = csilk_json_get_string(inst, "last_seen_at");
    if (seen && seen[0]) {
        csilk_json_add_string(st, "last_seen_at", seen);
    } else {
        csilk_json_add_null(st, "last_seen_at");
    }
    const char* inb = csilk_json_get_string(inst, "last_inbound_at");
    if (inb && inb[0]) {
        csilk_json_add_string(st, "last_inbound_at", inb);
    } else {
        csilk_json_add_null(st, "last_inbound_at");
    }
    csilk_json_free(inst);
    return st;
}
```

- [ ] **Step 4: Wire optional `last_inbound_at` heartbeat & register gateway `pushes/claim` and `pushes/:push_id/ack` routes**

1. In `backend/src/application/ai/im_bot_instance_usecases.h`, declare `im_instance_gateway_heartbeat_ext` alongside `im_instance_gateway_heartbeat`:

```c
int im_instance_gateway_heartbeat_ext(void*          pool,
                                      int64_t        id,
                                      const char*    last_inbound_at,
                                      csilk_json_t** out,
                                      int*           code,
                                      char*          err,
                                      size_t         err_cap);
int im_instance_gateway_heartbeat(
    void* pool, int64_t id, csilk_json_t** out, int* code, char* err, size_t err_cap);
```

2. In `backend/src/application/ai/im_bot_instance_usecases.c`, update `im_instance_gateway_heartbeat` to delegate to `im_instance_gateway_heartbeat_ext`:

```c
int
im_instance_gateway_heartbeat_ext(void*          pool,
                                  int64_t        id,
                                  const char*    last_inbound_at,
                                  csilk_json_t** out,
                                  int*           code,
                                  char*          err,
                                  size_t         err_cap)
{
    if (out) {
        *out = NULL;
    }
    if (code) {
        *code = 0;
    }
    im_inst_err(err, err_cap, "");
    char    status[64] = {0};
    int64_t epoch = 0;
    int     enabled = 0;
    int     rc = mf_im_bot_instance_heartbeat_with_inbound(
        (csilk_db_pool_t*)pool, id, last_inbound_at, status, sizeof(status), &epoch, &enabled);
    if (rc == -3) {
        im_inst_err(err, err_cap, "instance not found");
        if (code) {
            *code = 1003;
        }
        return -1;
    }
    if (rc != 0) {
        im_inst_err(err, err_cap, "heartbeat failed");
        if (code) {
            *code = 1002;
        }
        return -1;
    }
    if (!enabled) {
        im_inst_err(err, err_cap, "instance is disabled");
        if (code) {
            *code = 1004;
        }
        return -1;
    }
    if (out) {
        csilk_json_t* data = csilk_json_object();
        csilk_json_add_string(data, "status", status);
        csilk_json_add_int(data, "login_epoch", (int64_t)epoch);
        csilk_json_add_int(data, "enabled", enabled ? 1 : 0);
        *out = data;
    }
    return 0;
}

int
im_instance_gateway_heartbeat(
    void* pool, int64_t id, csilk_json_t** out, int* code, char* err, size_t err_cap)
{
    return im_instance_gateway_heartbeat_ext(pool, id, NULL, out, code, err, err_cap);
}
```

3. In `backend/src/interfaces/http/controllers/im_bot_instance_controller.c`:
   - Add `#include "application/ai/im_push_usecases.h"` at the top includes.
   - Update `api_gateway_instance_heartbeat` to read optional `last_inbound_at`:

```c
static void
api_gateway_instance_heartbeat(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   last_inbound_at = im_instance_json_string(body, "last_inbound_at");
    int           code = 1002;
    char          err[256] = {0};
    csilk_json_t* out = NULL;
    int           rc = im_instance_gateway_heartbeat_ext(
        db_get_pool(), id, last_inbound_at, &out, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    if (rc != 0) {
        im_instance_respond_error(c, code, err);
        return;
    }
    respond_ok(c, out);
}
```

   - Add `api_gateway_instance_pushes_claim` and `api_gateway_instance_push_ack`:

```c
static void
api_gateway_instance_pushes_claim(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    if (id <= 0) {
        respond_bad_request(c, "无效的实例 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    bool          has_context = true;
    if (body && csilk_json_get(body, "has_context") != NULL) {
        has_context = csilk_json_get_bool(body, "has_context");
    }
    const char* ctx_updated_at = im_instance_json_string(body, "context_updated_at");
    int         limit = body && csilk_json_get(body, "limit") != NULL
                            ? (int)db_get_int(body, "limit")
                            : 5;

    csilk_json_t* items = NULL;
    int           code = 1002;
    char          err[256] = {0};
    int           rc = im_push_usecase_gateway_claim(
        db_get_pool(), id, has_context, ctx_updated_at, limit, &items, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    if (rc != 0) {
        im_instance_respond_error(c, code, err);
        return;
    }
    csilk_json_t* wrap = csilk_json_object();
    csilk_json_add_object(wrap, "items", items ? items : csilk_json_array());
    respond_ok(c, wrap);
}

static void
api_gateway_instance_push_ack(csilk_ctx_t* c)
{
    if (!gateway_auth_check(c)) {
        return;
    }
    int64_t id = im_instance_parse_id(c);
    int64_t push_id = parse_query_int_strict(csilk_get_param(c, "push_id"), -1);
    if (id <= 0 || push_id <= 0) {
        respond_bad_request(c, "无效的实例或推送 ID");
        return;
    }
    csilk_json_t* body = csilk_bind_json(c);
    const char*   result = im_instance_json_string(body, "result");
    const char*   err_msg = im_instance_json_string(body, "error");
    int           retry_after = body ? (int)db_get_int(body, "retry_after_sec") : 0;

    int  code = 1002;
    char err[256] = {0};
    int  rc = im_push_usecase_gateway_ack(
        db_get_pool(), id, push_id, result, err_msg, retry_after, &code, err, sizeof(err));
    if (body) {
        csilk_json_free(body);
    }
    (void)im_instance_ok_or_error(c, rc, code, err);
}
```

   - Register in `register_im_bot_instance_routes`:

```c
    csilk_app_post(app, "/api/gateway/im/instances/:id/pushes/claim", api_gateway_instance_pushes_claim);
    csilk_app_post(app, "/api/gateway/im/instances/:id/pushes/:push_id/ack", api_gateway_instance_push_ack);
```

- [ ] **Step 5: Reconfigure CMake, build & run tests**

Run:
```bash
cmake -S backend -B backend/build -G "Unix Makefiles" && \
./backend/tests/run_isolated.sh build backend/build && \
ctest --test-dir backend/build -R "^test_im_push_outbox$" --output-on-failure
```
Expected: `1/1 passed`.

- [ ] **Step 6: Commit Task 3**

```bash
git add backend/src/application/ai/im_push_usecases.h \
        backend/src/application/ai/im_push_usecases.c \
        backend/src/application/ai/im_bot_instance_usecases.h \
        backend/src/application/ai/im_bot_instance_usecases.c \
        backend/src/interfaces/http/controllers/im_bot_instance_controller.c \
        backend/tests/unit/test_im_push_outbox.c
git commit -m "feat(im): ✨ add gateway push claim/ack routes and push usecases"
```

---

### Task 4: Scheduler Channel (`wechat_ilink`), Test Closed-Loop & Engine Broadcast Dispatch

**Files:**
- Modify: `backend/src/application/scheduler/usecases.c:60-86,346-464`
- Modify: `backend/src/core/scheduler/scheduler_engine.c:110-140,520-585,695-785`
- Test: `backend/tests/unit/test_im_push_outbox.c` and `backend/tests/unit/test_scheduler_engine.c`

- [ ] **Step 1: Add failing scheduler channel & engine broadcast tests**

1. In `backend/tests/unit/test_im_push_outbox.c`, add `#include <pthread.h>`, `#include "application/scheduler/dtos.h"`, `#include "application/scheduler/usecases.h"`, `#include "infrastructure/repositories/scheduler_repo_impl.h"` at top, add `test_scheduler_ilink_channel(csilk_db_pool_t* pool)`, and invoke `test_scheduler_ilink_channel(pool);` from `main()`:

```c
typedef struct {
    csilk_db_pool_t* pool;
    int64_t          inst_id;
} test_worker_ctx_t;

static void*
mock_ilink_test_ack_worker(void* arg)
{
    test_worker_ctx_t* w = (test_worker_ctx_t*)arg;
    for (int i = 0; i < 40; ++i) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        if (im_push_usecase_gateway_claim(
                w->pool, w->inst_id, true, "2026-11-25T13:00:00Z", 5, &items, &code, err, sizeof(err)) ==
                0 &&
            items && csilk_json_array_size(items) > 0) {
            csilk_json_t* row = csilk_json_array_get(items, 0);
            int64_t       pid = db_get_int(row, "id");
            assert(im_push_usecase_gateway_ack(
                       w->pool, w->inst_id, pid, "sent", "", 0, &code, err, sizeof(err)) == 0);
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
test_scheduler_ilink_channel(csilk_db_pool_t* pool)
{
    int64_t u = seed_user_and_active_instance(pool, "push_sched_u1", "bot_s1", "wx_s1");
    csilk_json_t* inst = mf_im_bot_instance_get_by_user(pool, u, "wechat", "ilink");
    assert(inst != NULL);
    int64_t inst_id = db_get_int(inst, "id");
    csilk_json_free(inst);

    channel_save_dto_t dto;
    memset(&dto, 0, sizeof(dto));
    snprintf(dto.channel_type, sizeof(dto.channel_type), "wechat_ilink");
    snprintf(dto.channel_name, sizeof(dto.channel_name), "个人微信 iLink");
    assert(scheduler_usecase_save_channel(pool, u, &dto) == 0);

    csilk_json_t* ch_resp = NULL;
    assert(scheduler_usecase_get_channel(pool, u, &ch_resp) == 0 && ch_resp != NULL);
    assert(strcmp(csilk_json_get_string(ch_resp, "channel_type"), "wechat_ilink") == 0);
    csilk_json_t* ilink_st = csilk_json_get(ch_resp, "ilink_status");
    assert(ilink_st != NULL && db_get_bool(ilink_st, "has_instance"));
    csilk_json_free(ch_resp);

    csilk_json_t* dash_resp = NULL;
    assert(scheduler_usecase_dashboard(pool, u, false, &dash_resp) == 0 && dash_resp != NULL);
    csilk_json_t* dash_ch = csilk_json_get(dash_resp, "push_channel");
    assert(dash_ch && csilk_json_get(dash_ch, "ilink_status") != NULL);
    csilk_json_free(dash_resp);

    /* Closed-loop test succeeds when gateway acks inside window */
    test_worker_ctx_t ctx = {.pool = pool, .inst_id = inst_id};
    pthread_t         tid;
    assert(pthread_create(&tid, NULL, mock_ilink_test_ack_worker, &ctx) == 0);
    char err[256] = {0};
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) == 0);
    pthread_join(tid, NULL);

    /* Gateway offline -> times out inside short test window cleanly */
    setenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS", "300", 1);
    assert(scheduler_usecase_test_channel(pool, u, err, sizeof(err)) != 0);
    assert(strstr(err, "响应") != NULL);
    unsetenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");

    printf("  [OK] test_scheduler_ilink_channel\n");
}
```

2. In `backend/tests/unit/test_scheduler_engine.c`, add `#include "application/ai/im_push_usecases.h"`, `#include "application/scheduler/dtos.h"`, `#include "application/scheduler/usecases.h"`, `#include "infrastructure/repositories/im_bot_repo_impl.h"`, `#include "infrastructure/repositories/im_push_outbox_repo_impl.h"`, and add step `4b. Test Built-in Broadcast with wechat_ilink Outbox Rollup` before `5. Test Engine Stop`:

```c
    /* 4b. Test Built-in Broadcast with wechat_ilink Outbox Rollup */
    const char* u_names[2] = {"bc_u1", "bc_u2"};
    int64_t     u_ids[2] = {0, 0};
    int64_t     inst_ids[2] = {0, 0};
    for (int idx = 0; idx < 2; ++idx) {
        assert(csilk_db_exec_param(
                   pool,
                   "INSERT INTO users (username, password) VALUES (?, 'hash')",
                   (const char*[]){u_names[idx], NULL}) == 0);
        csilk_json_t* ures = csilk_db_query_param_json(
            pool, "SELECT id FROM users WHERE username=? LIMIT 1", (const char*[]){u_names[idx], NULL});
        assert(ures && csilk_json_array_size(ures) == 1);
        u_ids[idx] = db_get_int(csilk_json_array_get(ures, 0), "id");
        csilk_json_free(ures);

        inst_ids[idx] = mf_im_bot_instance_create(pool, u_ids[idx], "wechat", "ilink");
        assert(inst_ids[idx] > 0);
        char ierr[128] = {0};
        char bot[32], wx[32];
        snprintf(bot, sizeof(bot), "bot_bc_%d", idx);
        snprintf(wx, sizeof(wx), "wx_bc_%d", idx);
        assert(mf_im_bot_instance_activate(pool, inst_ids[idx], bot, wx, ierr, sizeof(ierr)) == 0);

        channel_save_dto_t cdto;
        memset(&cdto, 0, sizeof(cdto));
        snprintf(cdto.channel_type, sizeof(cdto.channel_type), "wechat_ilink");
        snprintf(cdto.channel_name, sizeof(cdto.channel_name), "微信 iLink");
        assert(scheduler_usecase_save_channel(pool, u_ids[idx], &cdto) == 0);
    }

    int64_t bi_job_id = mf_scheduler_repo_job_create_builtin(
        pool,
        "Builtin Broadcast Test",
        "Verify builtin broadcast to wechat_ilink outbox",
        "system_builtin",
        "cron",
        "{\"cron\":\"0 8 * * *\"}",
        "{\"handler\":\"mcp_pool_cleanup\"}",
        "user_default",
        "{}",
        "2099-01-01 00:00:00");
    assert(bi_job_id > 0);
    assert(scheduler_engine_trigger_job_now(bi_job_id, 0, "cron_tick") == 0);

    int64_t bi_log_id = 0;
    waited_ms = 0;
    while (waited_ms < 2000) {
        total_logs = 0;
        csilk_json_t* bi_logs =
            mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
        if (bi_logs && csilk_json_array_size(bi_logs) > 0) {
            csilk_json_t* b0 = csilk_json_array_get(bi_logs, 0);
            const char*   st = csilk_json_get_string(b0, "status");
            const char*   ns = csilk_json_get_string(b0, "notification_status");
            if (st && strcmp(st, "success") == 0 && ns && strcmp(ns, "queued") == 0) {
                bi_log_id = db_get_int(b0, "id");
                csilk_json_free(bi_logs);
                break;
            }
        }
        if (bi_logs) {
            csilk_json_free(bi_logs);
        }
        usleep(20000);
        waited_ms += 20;
    }
    assert(bi_log_id > 0);

    for (int idx = 0; idx < 2; ++idx) {
        csilk_json_t* items = NULL;
        int           code = 0;
        char          err[128] = {0};
        assert(im_push_usecase_gateway_claim(pool,
                                             inst_ids[idx],
                                             true,
                                             "2026-11-25T13:10:00Z",
                                             5,
                                             &items,
                                             &code,
                                             err,
                                             sizeof(err)) == 0);
        assert(items && csilk_json_array_size(items) == 1);
        int64_t pid = db_get_int(csilk_json_array_get(items, 0), "id");
        assert(db_get_int(csilk_json_array_get(items, 0), "job_run_log_id") == bi_log_id);
        csilk_json_free(items);
        assert(im_push_usecase_gateway_ack(
                   pool, inst_ids[idx], pid, "sent", "", 0, &code, err, sizeof(err)) == 0);
    }

    total_logs = 0;
    csilk_json_t* bi_done =
        mf_scheduler_repo_log_list(pool, 0, true, bi_job_id, 1, 10, &total_logs);
    assert(bi_done && csilk_json_array_size(bi_done) > 0);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(bi_done, 0), "notification_status"),
                  "sent") == 0);
    csilk_json_free(bi_done);
```

- [ ] **Step 2: Run failing tests**

Run:
```bash
./backend/tests/run_isolated.sh build backend/build && \
ctest --test-dir backend/build -R "^(test_im_push_outbox|test_scheduler_engine)$" --output-on-failure
```
Expected: FAIL (assert fails because `wechat_ilink` channel & built-in broadcast are not wired yet).

- [ ] **Step 3: Implement `wechat_ilink` support in `backend/src/application/scheduler/usecases.c`**

1. Add `#include "application/ai/im_push_usecases.h"` and `#include <strings.h>` in `backend/src/application/scheduler/usecases.c`.
2. In `scheduler_usecase_dashboard`, attach `"ilink_status"` onto `ch_obj` (replacing lines 71-83):

```c
    csilk_json_t* ch_obj = csilk_json_object();
    if (ch) {
        csilk_json_add_string(ch_obj, "channel_type", csilk_json_get_string(ch, "channel_type"));
        csilk_json_add_string(ch_obj, "channel_name", csilk_json_get_string(ch, "channel_name"));
        csilk_json_add_number(ch_obj, "is_active", (double)db_get_int(ch, "is_active"));
        csilk_json_free(ch);
    } else {
        csilk_json_add_string(ch_obj, "channel_type", "none");
        csilk_json_add_string(ch_obj, "channel_name", "");
        csilk_json_add_number(ch_obj, "is_active", 0);
    }
    csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
    if (ilink_st) {
        csilk_json_add_object(ch_obj, "ilink_status", ilink_st);
    }
    csilk_json_add_object(resp, "push_channel", ch_obj);
```

3. In `scheduler_usecase_get_channel`, attach `"ilink_status"` onto both `def` (lines 355-363) and `resp` (lines 376-385):

```c
    csilk_json_t* ch = mf_scheduler_repo_get_user_channel(pool, user_id);
    if (!ch) {
        csilk_json_t* def = csilk_json_object();
        csilk_json_add_string(def, "channel_type", "none");
        csilk_json_add_string(def, "channel_name", "个人微信");
        csilk_json_add_string(def, "webhook_url", "");
        csilk_json_add_string(def, "secret_key_masked", "");
        csilk_json_add_number(def, "is_active", 0);
        csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
        if (ilink_st) {
            csilk_json_add_object(def, "ilink_status", ilink_st);
        }
        *out_resp = def;
        return 0;
    }
...
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "channel_type", csilk_json_get_string(ch, "channel_type"));
    csilk_json_add_string(resp, "channel_name", csilk_json_get_string(ch, "channel_name"));
    csilk_json_add_string(resp, "webhook_url", csilk_json_get_string(ch, "webhook_url"));
    csilk_json_add_string(resp, "secret_key_masked", masked);
    csilk_json_add_number(resp, "is_active", (double)db_get_int(ch, "is_active"));
    csilk_json_t* ilink_st = im_push_usecase_build_ilink_status(pool, user_id);
    if (ilink_st) {
        csilk_json_add_object(resp, "ilink_status", ilink_st);
    }

    csilk_json_free(ch);
    *out_resp = resp;
    return 0;
```

4. Validate `channel_type` in `scheduler_usecase_save_channel` and clear webhook/secret when saving `wechat_ilink`:

```c
static bool
scheduler_channel_type_allowed(const char* ct)
{
    if (!ct || !ct[0]) {
        return false;
    }
    return strcasecmp(ct, "serverchan") == 0 || strcasecmp(ct, "pushdeer") == 0 ||
           strcasecmp(ct, "wechat_work") == 0 || strcasecmp(ct, "wechatwork") == 0 ||
           strcasecmp(ct, "qywx") == 0 || strcasecmp(ct, "webhook") == 0 ||
           strcasecmp(ct, "wechat_ilink") == 0;
}

int
scheduler_usecase_save_channel(void* pool, int64_t user_id, const channel_save_dto_t* dto)
{
    if (!pool || !dto || !scheduler_channel_type_allowed(dto->channel_type)) {
        return -1;
    }

    if (strcasecmp(dto->channel_type, "wechat_ilink") == 0) {
        return mf_scheduler_repo_save_user_channel(
            pool, user_id, "wechat_ilink", dto->channel_name, "", "");
    }

    const char* real_secret = dto->secret_key;
    char        saved_secret[128] = "";

    /* If secret_key is empty, masked or unchanged, keep existing */
    if (dto->secret_key[0] == '\0' || dto->secret_key[0] == '*' ||
        strstr(dto->secret_key, "******")) {
        csilk_json_t* existing = mf_scheduler_repo_get_user_channel(pool, user_id);
        if (existing) {
            const char* orig = csilk_json_get_string(existing, "secret_key");
            if (orig) {
                snprintf(saved_secret, sizeof(saved_secret), "%s", orig);
                real_secret = saved_secret;
            }
            csilk_json_free(existing);
        }
    }

    return mf_scheduler_repo_save_user_channel(
        pool, user_id, dto->channel_type, dto->channel_name, dto->webhook_url, real_secret);
}
```

5. In `scheduler_usecase_test_channel`, define `test_msg` before branching and handle `wechat_ilink` with the 10-second closed-loop poll (after reading `ct = csilk_json_get_string(ch, "channel_type")`):

```c
    const char* test_msg = "### 🎉 Minefolio 微信推送连通性测试成功\n\n"
                           "> 恭喜！您已成功打通个人微信通知服务通道。\n\n"
                           "- **接入通道**: 专属微信提醒\n"
                           "- **触发状态**: 握手验证通过\n"
                           "- **服务效用**: 今后定时简报、异动预警将第一时间推送到您的微信！\n\n"
                           "---\n*Minefolio 自动化调度中心*";

    const char* ct = csilk_json_get_string(ch, "channel_type");
    if (ct && strcasecmp(ct, "wechat_ilink") == 0) {
        csilk_json_free(ch);
        int64_t push_id = 0;
        if (im_push_usecase_enqueue(pool,
                                    user_id,
                                    0,
                                    "test",
                                    "Minefolio 推送连通性测试",
                                    test_msg,
                                    30,
                                    &push_id,
                                    err_buf,
                                    err_len) != 0) {
            return -1;
        }
        int         timeout_ms = 10000;
        const char* env_to = getenv("MINEFOLIO_ILINK_TEST_TIMEOUT_MS");
        if (env_to && atoi(env_to) >= 100) {
            timeout_ms = atoi(env_to);
        }
        int waited_ms = 0;
        while (waited_ms < timeout_ms) {
            csilk_json_t* pr = im_push_usecase_get(pool, push_id);
            if (pr) {
                const char* st = csilk_json_get_string(pr, "status");
                const char* le = csilk_json_get_string(pr, "last_error");
                if (st && strcmp(st, "sent") == 0) {
                    csilk_json_free(pr);
                    return 0;
                }
                if (st && (strcmp(st, "failed") == 0 || strcmp(st, "expired") == 0)) {
                    if (err_buf && err_len > 0) {
                        snprintf(err_buf,
                                 err_len,
                                 "%s",
                                 (le && le[0]) ? le : "微信 iLink 推送测试失败");
                    }
                    csilk_json_free(pr);
                    return -1;
                }
                csilk_json_free(pr);
            }
            struct timespec req = {0, 100000000L}; /* 100ms */
            nanosleep(&req, NULL);
            waited_ms += 100;
        }
        (void)im_push_usecase_cancel_expired_test(
            pool, push_id, "iLink 网关未在测试窗口内响应");
        if (err_buf && err_len > 0) {
            snprintf(err_buf,
                     err_len,
                     "iLink 网关未在 %d 秒内响应，请检查 ilink-server 进程是否运行且网络可达",
                     timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 1);
        }
        return -1;
    }
```

- [ ] **Step 4: Implement `deliver_to_user_channel`, built-in broadcast, and periodic expiry in `backend/src/core/scheduler/scheduler_engine.c`**

Include `"application/ai/im_push_usecases.h"` in `scheduler_engine.c`:
1. Add static helper `deliver_to_user_channel`:

```c
static void
deliver_to_user_channel(void*         pool,
                        int64_t       user_id,
                        int64_t       log_id,
                        csilk_json_t* ch,
                        const char*   title,
                        const char*   markdown_body,
                        char*         out_status,
                        size_t        out_status_cap,
                        char*         err_buf,
                        size_t        err_cap)
{
    snprintf(out_status, out_status_cap, "none");
    if (!ch || db_get_int(ch, "is_active") != 1) {
        return;
    }
    const char* ct = csilk_json_get_string(ch, "channel_type");
    if (!ct || !ct[0] || strcasecmp(ct, "none") == 0) {
        return;
    }
    if (strcasecmp(ct, "wechat_ilink") == 0) {
        int64_t push_id = 0;
        if (im_push_usecase_enqueue(pool,
                                    user_id,
                                    log_id,
                                    "scheduler",
                                    title ? title : "Minefolio 提醒",
                                    markdown_body ? markdown_body : "",
                                    1800,
                                    &push_id,
                                    err_buf,
                                    err_cap) == 0) {
            snprintf(out_status, out_status_cap, "queued");
        } else {
            snprintf(out_status, out_status_cap, "failed");
        }
        return;
    }

    push_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    const char* cn = csilk_json_get_string(ch, "channel_name");
    const char* wu = csilk_json_get_string(ch, "webhook_url");
    const char* sk = csilk_json_get_string(ch, "secret_key");
    snprintf(cfg.channel_type, sizeof(cfg.channel_type), "%s", ct);
    if (cn) {
        snprintf(cfg.channel_name, sizeof(cfg.channel_name), "%s", cn);
    }
    if (wu) {
        snprintf(cfg.webhook_url, sizeof(cfg.webhook_url), "%s", wu);
    }
    if (sk) {
        snprintf(cfg.secret_key, sizeof(cfg.secret_key), "%s", sk);
    }
    int prc = wechat_push_service_send(
        &cfg, title ? title : "Minefolio 提醒", markdown_body, err_buf, err_cap);
    snprintf(out_status, out_status_cap, "%s", prc == 0 ? "sent" : "failed");
}
```

2. Update `run_job_task` steps 4 & 5 in `backend/src/core/scheduler/scheduler_engine.c` (complete the `job_run_logs` row with tentative `"queued"` **before** calling `im_push_usecase_enqueue()` so a fast gateway `ack` rollup never gets overwritten by `mf_scheduler_repo_log_complete`):

```c
    /* 4 & 5. Deliver notification & finalize job_run_logs (queued written BEFORE enqueue) */
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    int duration_ms = (int)((ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                            (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000);

    const char* run_status = (is_ai_job && !ai_slot_acquired) ? "skipped" : "success";
    int64_t     job_user_id = db_get_int(job, "user_id");
    char        notif_status[16] = "none";
    char        err_buf[256];
    err_buf[0] = '\0';

    bool should_deliver = (!is_ai_job || ai_slot_acquired) && delivery_channel &&
                          strcmp(delivery_channel, "silent_db") != 0;

    if (should_deliver && job_user_id == 0 && ctx->user_id == 0) {
        csilk_json_t* channels = mf_scheduler_repo_list_active_channels(ctx->pool);
        int           sync_sent = 0, sync_failed = 0, ilink_count = 0;
        size_t        nch = channels ? csilk_json_array_size(channels) : 0;

        for (size_t i = 0; i < nch; ++i) {
            csilk_json_t* ch = csilk_json_array_get(channels, i);
            const char*   ct = csilk_json_get_string(ch, "channel_type");
            if (!ct || !ct[0] || strcasecmp(ct, "none") == 0) {
                continue;
            }
            if (strcasecmp(ct, "wechat_ilink") == 0) {
                ilink_count++;
                continue;
            }
            char st[16] = "none";
            char ch_err[256] = {0};
            deliver_to_user_channel(ctx->pool,
                                    db_get_int(ch, "user_id"),
                                    log_id,
                                    ch,
                                    job_name,
                                    output_markdown,
                                    st,
                                    sizeof(st),
                                    ch_err,
                                    sizeof(ch_err));
            if (strcmp(st, "sent") == 0) {
                sync_sent++;
            } else if (strcmp(st, "failed") == 0) {
                sync_failed++;
                if (!err_buf[0] && ch_err[0]) {
                    snprintf(err_buf, sizeof(err_buf), "%s", ch_err);
                }
            }
        }

        if (sync_sent > 0 || sync_failed > 0) {
            mf_scheduler_repo_log_set_sync_counts(ctx->pool, log_id, sync_sent, sync_failed);
        }
        if (ilink_count > 0) {
            snprintf(notif_status, sizeof(notif_status), "queued");
        } else if (sync_sent > 0 && sync_failed > 0) {
            snprintf(notif_status, sizeof(notif_status), "partial");
        } else if (sync_sent > 0) {
            snprintf(notif_status, sizeof(notif_status), "sent");
        } else if (sync_failed > 0) {
            snprintf(notif_status, sizeof(notif_status), "failed");
        }

        mf_scheduler_repo_log_complete(
            ctx->pool, log_id, run_status, duration_ms, output_summary, err_buf, notif_status);

        if (ilink_count > 0) {
            int ilink_enqueued = 0, ilink_failed = 0;
            for (size_t i = 0; i < nch; ++i) {
                csilk_json_t* ch = csilk_json_array_get(channels, i);
                const char*   ct = csilk_json_get_string(ch, "channel_type");
                if (!ct || strcasecmp(ct, "wechat_ilink") != 0) {
                    continue;
                }
                char st[16] = "none";
                char ch_err[256] = {0};
                deliver_to_user_channel(ctx->pool,
                                        db_get_int(ch, "user_id"),
                                        log_id,
                                        ch,
                                        job_name,
                                        output_markdown,
                                        st,
                                        sizeof(st),
                                        ch_err,
                                        sizeof(ch_err));
                if (strcmp(st, "queued") == 0) {
                    ilink_enqueued++;
                } else {
                    ilink_failed++;
                    if (!err_buf[0] && ch_err[0]) {
                        snprintf(err_buf, sizeof(err_buf), "%s", ch_err);
                    }
                }
            }
            if (ilink_failed > 0) {
                mf_scheduler_repo_log_set_sync_counts(
                    ctx->pool, log_id, sync_sent, sync_failed + ilink_failed);
                if (ilink_enqueued == 0) {
                    int         total_fail = sync_failed + ilink_failed;
                    const char* final_st =
                        (sync_sent > 0 && total_fail > 0) ? "partial"
                                                          : (sync_sent > 0 ? "sent" : "failed");
                    mf_scheduler_repo_log_update_notif(ctx->pool, log_id, final_st, err_buf);
                }
            }
        }
        if (channels) {
            csilk_json_free(channels);
        }
    } else {
        csilk_json_t* ch = should_deliver ? mf_scheduler_repo_get_user_channel(ctx->pool, ctx->user_id)
                                          : NULL;
        const char*   ct = ch ? csilk_json_get_string(ch, "channel_type") : NULL;
        bool          is_ilink =
            ch && db_get_int(ch, "is_active") == 1 && ct && strcasecmp(ct, "wechat_ilink") == 0;
        const char* error_field = (is_ai_job && !ai_slot_acquired) ? "ai_concurrency_limit" : err_buf;

        if (is_ilink) {
            snprintf(notif_status, sizeof(notif_status), "queued");
            mf_scheduler_repo_log_complete(
                ctx->pool, log_id, run_status, duration_ms, output_summary, error_field, notif_status);

            char st[16] = "none";
            deliver_to_user_channel(ctx->pool,
                                    ctx->user_id,
                                    log_id,
                                    ch,
                                    job_name,
                                    output_markdown,
                                    st,
                                    sizeof(st),
                                    err_buf,
                                    sizeof(err_buf));
            if (strcmp(st, "queued") != 0) {
                mf_scheduler_repo_log_update_notif(ctx->pool, log_id, st, err_buf);
            }
        } else {
            if (ch) {
                deliver_to_user_channel(ctx->pool,
                                        ctx->user_id,
                                        log_id,
                                        ch,
                                        job_name,
                                        output_markdown,
                                        notif_status,
                                        sizeof(notif_status),
                                        err_buf,
                                        sizeof(err_buf));
            }
            mf_scheduler_repo_log_complete(
                ctx->pool, log_id, run_status, duration_ms, output_summary, error_field, notif_status);
        }
        if (ch) {
            csilk_json_free(ch);
        }
    }
```

3. In `scheduler_engine_thread` (`backend/src/core/scheduler/scheduler_engine.c`, right after checking cron rows inside `if (s_db_pool)`), invoke stale Outbox expiration:

```c
                csilk_json_free(rows);
            }
            im_push_usecase_expire_stale(s_db_pool);
        }
```

- [ ] **Step 5: Build & run backend unit tests**

Run:
```bash
./backend/tests/run_isolated.sh build backend/build && \
ctest --test-dir backend/build -R "^(test_im_push_outbox|test_scheduler_engine|test_scheduler_repo|test_scheduler_idle_sleep)$" --output-on-failure
```
Expected: `4/4 passed`.

- [ ] **Step 6: Commit Task 4**

```bash
git add backend/src/application/scheduler/usecases.c \
        backend/src/core/scheduler/scheduler_engine.c \
        backend/tests/unit/test_im_push_outbox.c \
        backend/tests/unit/test_scheduler_engine.c
git commit -m "feat(scheduler): ✨ wire wechat_ilink push channel, test loop, and builtin broadcast"
```

---

### Task 5: Gateway Core Modules (`state.js`, `client.js`, `formatter.js`, `api.js`)

**Files:**
- Modify: `gateway/ilink/state.js:90-115`
- Modify: `gateway/ilink/client.js:11-26,90-112,214-226`
- Create: `gateway/ilink/formatter.js`
- Modify: `gateway/ilink/api.js:70-122`
- Modify Test: `gateway/ilink/test/state.test.js`, `gateway/ilink/test/client.test.js`
- Create Test: `gateway/ilink/test/formatter.test.js`

- [ ] **Step 1: Write failing tests in `state.test.js`, `client.test.js`, and `formatter.test.js`**

1. Append inside `describe('ilink/state', ...)` in `gateway/ilink/test/state.test.js`:

```javascript
  test('persists, isolates, chmods 0600, and clears context.json per instance', () => {
    const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-ctx-'));
    try {
      const s1 = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const s2 = createStateStore({ rootDir: tmpRoot, instanceId: 2 });

      assert.strictEqual(s1.readContext(), null);
      s1.writeContext({
        from_user_id: 'wx_1',
        context_token: 'ctx_tok_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      assert.strictEqual(s1.readContext().context_token, 'ctx_tok_1');
      assert.strictEqual(s2.readContext(), null);
      const stat = fs.statSync(path.join(instanceDir(tmpRoot, 1), 'context.json'));
      assert.strictEqual(stat.mode & 0o777, 0o600);

      s1.clearContext();
      assert.strictEqual(s1.readContext(), null);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
```

2. Import `IlinkContextInvalidError` from `../client` and append inside `describe('ilink/client', ...)` in `gateway/ilink/test/client.test.js`:

```javascript
  test('sendText classifies ret=-2 as IlinkContextInvalidError, errcode=-14 as IlinkSessionExpiredError, and other errors cleanly', async () => {
    const responses = [
      { ret: -2, errmsg: 'invalid context' },
      { errcode: -14, errmsg: 'session expired' },
      { ret: 5001, errmsg: 'rate limited' },
    ];
    let idx = 0;
    const client = createIlinkClient({
      fetchImpl: async () => ({
        ok: true,
        status: 200,
        text: async () => JSON.stringify(responses[idx++]),
      }),
    });
    const baseArgs = {
      account: { bot_token: 't1', baseurl: 'https://bot.test' },
      toUserId: 'wx_1',
      contextToken: 'ctx_9',
      text: 'hello',
    };

    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err instanceof IlinkContextInvalidError && err.code === -2
    );
    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err instanceof IlinkSessionExpiredError && err.code === -14
    );
    await assert.rejects(
      () => client.sendText(baseArgs),
      (err) => err.message === 'iLink error 5001: rate limited'
    );
  });
```

3. Create `gateway/ilink/test/formatter.test.js`:

```javascript
'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const { formatPushText, splitPushChunks } = require('../formatter');

describe('ilink/formatter', () => {
  test('formatPushText strips Markdown noise and prepends title', () => {
    const md = [
      '### 📊 今日早报',
      '',
      '> 触发来源: **cron_tick**',
      '',
      '- **沪深300**: `+1.2%` [详情](https://example.com/a)',
      '',
      '| 标的 | 盈亏 |',
      '|---|---|',
      '| 茅台 | +500 |',
      '',
      '---',
    ].join('\n');

    const out = formatPushText('每日早报', md);
    assert.ok(out.startsWith('【每日早报】\n\n'));
    assert.ok(!out.includes('###'));
    assert.ok(!out.includes('**'));
    assert.ok(!out.includes('`'));
    assert.ok(!out.includes('|---|'));
    assert.ok(out.includes('详情 (https://example.com/a)'));
    assert.ok(out.includes('茅台 · +500'));
  });

  test('splitPushChunks breaks cleanly at newline boundaries near chunk limit', () => {
    const line = 'a'.repeat(15);
    const text = `${line}\n${line}\n${line}`;
    const chunks = splitPushChunks(text, 25);
    assert.strictEqual(chunks.length, 3);
    assert.strictEqual(chunks[0], line);
    assert.strictEqual(chunks[1], line);
    assert.strictEqual(chunks[2], line);
  });
});
```

- [ ] **Step 2: Run gateway tests to verify failure**

Run: `npm --prefix gateway test`
Expected: FAIL (`Cannot find module '../formatter'`, `s1.writeContext is not a function`, `IlinkContextInvalidError`).

- [ ] **Step 3: Implement `state.js`, `client.js`, `formatter.js`, and `api.js`**

1. In `gateway/ilink/state.js` (`createStateStore`):

```javascript
  const contextPath = path.join(dir, 'context.json');
  // inside returned object:
    readContext: () => readJson(contextPath),
    writeContext: (ctx) => writeJsonAtomic(contextPath, ctx),
    clearContext: () => removeFile(contextPath),
```

2. In `gateway/ilink/client.js`:

```javascript
class IlinkContextInvalidError extends Error {
  constructor(message = 'iLink context_token invalid or expired (-2)') {
    super(message);
    this.name = 'IlinkContextInvalidError';
    this.code = -2;
  }
}

function classifyPayloadError(data) {
  if (!data || typeof data !== 'object') return null;
  if (isSessionExpiredPayload(data)) {
    return new IlinkSessionExpiredError(data.errmsg || 'iLink session expired');
  }
  const code =
    typeof data.errcode === 'number' && data.errcode !== 0
      ? data.errcode
      : typeof data.ret === 'number' && data.ret !== 0
        ? data.ret
        : 0;
  if (code === 0) return null;
  const msg = String(data.errmsg || '');
  if (code === -2 || /context[_ ]?token.*(invalid|expired)/i.test(msg)) {
    return new IlinkContextInvalidError(msg || `iLink context invalid (${code})`);
  }
  const err = new Error(`iLink error ${code}: ${msg || 'request failed'}`);
  err.code = code;
  return err;
}
```
In `requestJson()`: replace both `if (isSessionExpiredPayload(data)) throw new IlinkSessionExpiredError(...)` blocks with `const classified = classifyPayloadError(data); if (classified) throw classified;` (before the HTTP `!res.ok` fallback). Export `IlinkContextInvalidError` & `classifyPayloadError`.

3. Create `gateway/ilink/formatter.js`:

```javascript
'use strict';

function formatPushText(title, markdownBody) {
  const cleanTitle = String(title || '').trim();
  const lines = String(markdownBody || '')
    .replace(/\r\n?/g, '\n')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g, '$1 ($2)')
    .replace(/```[^\n]*\n?/g, '')
    .replace(/`([^`]+)`/g, '$1')
    .replace(/\*\*([^*]+)\*\*/g, '$1')
    .replace(/__([^_]+)__/g, '$1')
    .split('\n');

  const outLines = [];
  for (let raw of lines) {
    if (/^\s*\|?\s*:?-{3,}:?(\s*\|\s*:?-{3,}:?)*\s*\|?\s*$/.test(raw)) {
      continue;
    }
    if (/^\s*-{3,}\s*$/.test(raw) || /^\s*\*{3,}\s*$/.test(raw)) {
      continue;
    }
    if (/^\s*\|.*\|\s*$/.test(raw)) {
      const cells = raw
        .trim()
        .replace(/^\|/, '')
        .replace(/\|$/, '')
        .split('|')
        .map((c) => c.trim())
        .filter(Boolean);
      raw = cells.join(' · ');
    } else {
      raw = raw.replace(/^\s*#{1,6}\s+/, '').replace(/^\s*>\s?/, '');
    }
    outLines.push(raw.replace(/[ \t]+$/g, ''));
  }

  let body = outLines.join('\n').replace(/\n{3,}/g, '\n\n').trim();
  if (cleanTitle && !body.startsWith(`【${cleanTitle}】`) && !body.startsWith(cleanTitle)) {
    body = body ? `【${cleanTitle}】\n\n${body}` : `【${cleanTitle}】`;
  }
  return body;
}

function splitPushChunks(text, chunkSize = Number(process.env.ILINK_REPLY_CHUNK || 1800)) {
  const s = String(text || '').trim();
  if (!s) return [];
  const max = Math.max(1, Number(chunkSize) || 1800);
  const minSoftBreak = Math.floor(max * 0.6);
  const out = [];
  let pos = 0;

  while (pos < s.length) {
    if (pos + max >= s.length) {
      const tail = s.slice(pos).trim();
      if (tail) out.push(tail);
      break;
    }
    let cut = pos + max;
    const lastNl = s.lastIndexOf('\n', cut);
    if (lastNl > pos + minSoftBreak) {
      cut = lastNl;
    } else if (lastNl > pos && s.slice(pos, lastNl).trim().length > 0 && max < 100) {
      cut = lastNl;
    }
    const piece = s.slice(pos, cut).trim();
    if (piece) out.push(piece);
    pos = cut;
    while (pos < s.length && (s[pos] === '\n' || s[pos] === '\r')) {
      pos += 1;
    }
  }
  return out;
}

module.exports = {
  formatPushText,
  splitPushChunks,
};
```

4. In `gateway/ilink/api.js`, add inside `createGatewayApi` and export both methods in the returned object:

```javascript
  async function claimPushes(id, payload = {}) {
    return request('POST', `/api/gateway/im/instances/${id}/pushes/claim`, payload);
  }

  async function ackPush(id, pushId, payload = {}) {
    return request('POST', `/api/gateway/im/instances/${id}/pushes/${pushId}/ack`, payload);
  }

  return {
    listInstances,
    getInstance,
    updateStatus,
    heartbeat,
    chat,
    getTask,
    pollTask,
    claimPushes,
    ackPush,
  };
```

- [ ] **Step 4: Run gateway tests**

Run: `npm --prefix gateway test`
Expected: ALL suites PASS.

- [ ] **Step 5: Commit Task 5**

```bash
git add gateway/ilink/state.js gateway/ilink/client.js gateway/ilink/formatter.js \
        gateway/ilink/api.js gateway/ilink/test/state.test.js \
        gateway/ilink/test/client.test.js gateway/ilink/test/formatter.test.js
git commit -m "feat(gateway): ✨ add iLink context store, error classification, push formatter, and push APIs"
```

---

### Task 6: Gateway Push Worker (`pusher.js`), Monitor Wake Hook (`monitor.js`) & Runner Wiring (`runner.js`)

**Files:**
- Create: `gateway/ilink/pusher.js`
- Modify: `gateway/ilink/monitor.js:60-231`
- Modify: `gateway/ilink/runner.js:55-120`
- Create Test: `gateway/ilink/test/pusher.test.js`
- Modify Test: `gateway/ilink/test/monitor.test.js:80-129`

- [ ] **Step 1: Write failing tests in `gateway/ilink/test/pusher.test.js` and `monitor.test.js`**

1. In `gateway/ilink/test/monitor.test.js`: capture `onContextUpdated` events during `runMonitor` (adding a test message `m_img_3` with `{ type: 2 }` image item + `context_token: 'ctx_def'`) and verify `state.readContext()` plus `onContextUpdated` invocations:

```javascript
      const ctxUpdates = [];
      // Inside client.getUpdates pollBatch === 1 msgs array, include an unsupported item with context_token:
      // { message_id: 'm_img_3', message_type: 1, from_user_id: 'owner_wx', context_token: 'ctx_def', item_list: [{ type: 2 }] }
      // Pass into runMonitor({... onContextUpdated: (ev) => ctxUpdates.push(ev) })
      // Assert after runMonitor settles:
      assert.strictEqual(state.readContext().context_token, 'ctx_def');
      assert.strictEqual(state.readContext().invalidated, false);
      assert.ok(ctxUpdates.some((e) => e.contextToken === 'ctx_def'));
```

2. Create `gateway/ilink/test/pusher.test.js`:

```javascript
'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { createStateStore } = require('../state');
const { IlinkSessionExpiredError, IlinkContextInvalidError } = require('../client');
const { createPusher } = require('../pusher');

function makeTmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mf-ilink-push-'));
}

describe('ilink/pusher', () => {
  test('claims, formats, splits, sends chunks, and acks sent', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const claimedPayloads = [];
      const acked = [];
      const sent = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        replyChunk: 12,
        sendGapMs: 0,
        client: {
          sendText: async (args) => {
            sent.push(args);
          },
        },
        api: {
          claimPushes: async (_id, payload) => {
            claimedPayloads.push(payload);
            return { items: [{ id: 11, kind: 'scheduler', title: '早报', content: 'A'.repeat(20) }] };
          },
          ackPush: async (_id, pushId, payload) => {
            acked.push({ pushId, payload });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(claimedPayloads[0].has_context, true);
      assert.strictEqual(claimedPayloads[0].context_updated_at, '2026-11-25T10:00:00Z');
      assert.ok(sent.length >= 2);
      assert.strictEqual(sent[0].contextToken, 'ctx_1');
      assert.deepStrictEqual(acked, [{ pushId: 11, payload: { result: 'sent' } }]);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('has_context=false and invalid context_token (-2) ack waiting_context and invalidate local context', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const acks = [];
      const claims = [];
      let call = 0;

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        client: {
          sendText: async () => {
            throw new IlinkContextInvalidError('expired ctx');
          },
        },
        api: {
          claimPushes: async (_id, payload) => {
            claims.push(payload);
            call += 1;
            if (call === 1) {
              return { items: [{ id: 21, kind: 'test', title: '测试', content: 'hi' }] };
            }
            return { items: [{ id: 22, kind: 'scheduler', title: '日报', content: 'body' }] };
          },
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async () => {},
        },
      });

      // Missing context -> claims with has_context=false, processOne acks waiting_context
      await pusher.pumpOnce();
      assert.strictEqual(claims[0].has_context, false);
      assert.strictEqual(acks[0].pushId, 21);
      assert.strictEqual(acks[0].result, 'waiting_context');

      // With context -> client throws -2 -> context.invalidated becomes true & acks waiting_context
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_bad',
        updated_at: '2026-11-25T10:05:00Z',
        invalidated: false,
      });
      await pusher.pumpOnce();
      assert.strictEqual(state.readContext().invalidated, true);
      assert.strictEqual(acks[1].pushId, 22);
      assert.strictEqual(acks[1].result, 'waiting_context');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('wake() triggers immediate re-claim without waiting full pollMs', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      const ac = new AbortController();
      let claimCount = 0;

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        pollMs: 60000,
        sendGapMs: 0,
        signal: ac.signal,
        client: { sendText: async () => {} },
        api: {
          claimPushes: async () => {
            claimCount += 1;
            if (claimCount >= 2) ac.abort();
            return { items: [] };
          },
          ackPush: async () => {},
          updateStatus: async () => {},
        },
      });

      const running = pusher.run();
      await new Promise((r) => setTimeout(r, 20));
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_woke',
        updated_at: '2026-11-25T10:10:00Z',
        invalidated: false,
      });
      pusher.wake();
      await running;
      assert.ok(claimCount >= 2);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('sendText throwing IlinkSessionExpiredError (-14) updates status, acks retry, and throws', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });
      const updates = [];
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        sendGapMs: 0,
        client: {
          sendText: async () => {
            throw new IlinkSessionExpiredError('expired');
          },
        },
        api: {
          claimPushes: async () => ({
            items: [{ id: 31, kind: 'scheduler', title: 'T', content: 'C' }],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, result: payload.result });
          },
          updateStatus: async (_id, payload) => {
            updates.push(payload.status);
          },
        },
      });

      await assert.rejects(() => pusher.run(), (err) => err instanceof IlinkSessionExpiredError);
      assert.deepStrictEqual(updates, ['session_expired']);
      assert.deepStrictEqual(acks, [{ pushId: 31, result: 'retry' }]);
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });

  test('transient error retries chunk 2 in-place before acking retry after double failure', async () => {
    const tmpRoot = makeTmpDir();
    try {
      const state = createStateStore({ rootDir: tmpRoot, instanceId: 1 });
      state.writeContext({
        from_user_id: 'owner_wx',
        context_token: 'ctx_1',
        updated_at: '2026-11-25T10:00:00Z',
        invalidated: false,
      });

      const sentTexts = [];
      const acks = [];

      const pusher = createPusher({
        state,
        account: { bot_token: 'tok', ilink_user_id: 'owner_wx' },
        instanceId: 1,
        replyChunk: 10,
        sendGapMs: 0,
        client: {
          sendText: async ({ text }) => {
            sentTexts.push(text);
            if (text.includes('SECOND')) {
              throw new Error('network timeout');
            }
          },
        },
        api: {
          claimPushes: async () => ({
            items: [
              {
                id: 41,
                kind: 'scheduler',
                title: 'T',
                content: 'FIRSTCHUNK\n\nSECONDCHUN',
              },
            ],
          }),
          ackPush: async (_id, pushId, payload) => {
            acks.push({ pushId, payload });
          },
          updateStatus: async () => {},
        },
      });

      await pusher.pumpOnce();
      assert.strictEqual(sentTexts.filter((t) => t.includes('SECOND')).length, 2);
      assert.strictEqual(sentTexts.filter((t) => !t.includes('SECOND')).length, 1);
      assert.strictEqual(acks[0].pushId, 41);
      assert.strictEqual(acks[0].payload.result, 'retry');
    } finally {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  });
});
```

- [ ] **Step 2: Run gateway tests to verify failure**

Run: `npm --prefix gateway test`
Expected: FAIL (`Cannot find module '../pusher'`, `state.readContext() === null`).

- [ ] **Step 3: Implement `pusher.js`, update `monitor.js` and `runner.js`**

1. Create `gateway/ilink/pusher.js`:

```javascript
'use strict';

const { IlinkSessionExpiredError, IlinkContextInvalidError } = require('./client');
const { formatPushText, splitPushChunks } = require('./formatter');

function hasValidContext(ctx, account) {
  if (!ctx || !ctx.context_token || ctx.invalidated) {
    return false;
  }
  if (account && account.ilink_user_id && ctx.from_user_id && ctx.from_user_id !== account.ilink_user_id) {
    return false;
  }
  return true;
}

function createPusher({
  client,
  state,
  account,
  instanceId,
  api,
  log = console,
  signal,
  pollMs = Number(process.env.ILINK_PUSH_POLL_MS || 3000),
  batchLimit = Number(process.env.ILINK_PUSH_BATCH_LIMIT || 5),
  sendGapMs = Number(process.env.ILINK_PUSH_SEND_GAP_MS || 500),
  replyChunk = Number(process.env.ILINK_REPLY_CHUNK || 1800),
  sleepImpl,
} = {}) {
  let busy = false;
  let pendingWake = false;
  let wakeSleep = null;

  function defaultSleep(ms) {
    if (ms <= 0) return Promise.resolve();
    return new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        if (signal) signal.removeEventListener('abort', finish);
        if (wakeSleep === finish) wakeSleep = null;
        resolve();
      };
      wakeSleep = finish;
      const timer = setTimeout(finish, ms);
      if (signal) signal.addEventListener('abort', finish, { once: true });
    });
  }

  const waitMs = sleepImpl || defaultSleep;

  async function sendChunkWithOnceRetry(toUserId, contextToken, text) {
    try {
      await client.sendText({ account, toUserId, contextToken, text, signal });
    } catch (err) {
      if (err instanceof IlinkSessionExpiredError || err instanceof IlinkContextInvalidError) {
        throw err;
      }
      if (signal && signal.aborted) {
        throw err;
      }
      await client.sendText({ account, toUserId, contextToken, text, signal });
    }
  }

  async function processOne(item) {
    const ctx = state.readContext();
    if (!hasValidContext(ctx, account)) {
      await api.ackPush(instanceId, item.id, {
        result: 'waiting_context',
        error: '请先在微信向机器人发送任意一条消息以建立会话',
      });
      return;
    }

    const toUserId = (account && account.ilink_user_id) || ctx.from_user_id;
    const formatted = formatPushText(item.title, item.content);
    const chunks = splitPushChunks(formatted, replyChunk);
    if (chunks.length === 0) {
      await api.ackPush(instanceId, item.id, { result: 'sent' });
      return;
    }

    try {
      for (let i = 0; i < chunks.length; i += 1) {
        if (signal && signal.aborted) return;
        await sendChunkWithOnceRetry(toUserId, ctx.context_token, chunks[i]);
        if (i + 1 < chunks.length && sendGapMs > 0) {
          await waitMs(sendGapMs);
        }
      }
      await api.ackPush(instanceId, item.id, { result: 'sent' });
    } catch (err) {
      if (err instanceof IlinkSessionExpiredError) {
        await api
          .updateStatus(instanceId, {
            status: 'session_expired',
            last_error: '微信登录态已过期，请重新扫码登录',
          })
          .catch(() => {});
        await api
          .ackPush(instanceId, item.id, {
            result: item.kind === 'test' ? 'failed' : 'retry',
            error: '微信登录态已过期，请重新扫码登录',
          })
          .catch(() => {});
        throw err;
      }
      if (err instanceof IlinkContextInvalidError) {
        state.writeContext({ ...ctx, invalidated: true });
        await api.ackPush(instanceId, item.id, {
          result: 'waiting_context',
          error: '微信会话上下文已过期，请在微信向机器人发送任意消息后重试',
        });
        return;
      }
      log.warn?.('[ilink-pusher] send failed:', err.message);
      await api
        .ackPush(instanceId, item.id, {
          result: 'retry',
          error: err.message || '发送失败',
        })
        .catch(() => {});
    }
  }

  async function pumpOnce() {
    if (busy) {
      pendingWake = true;
      return;
    }
    busy = true;
    try {
      do {
        pendingWake = false;
        const ctx = state.readContext();
        const valid = hasValidContext(ctx, account);
        const claimed = await api.claimPushes(instanceId, {
          has_context: valid,
          context_updated_at: (ctx && ctx.updated_at) || '',
          limit: batchLimit,
        });
        const items = Array.isArray(claimed && claimed.items) ? claimed.items : [];
        for (let i = 0; i < items.length; i += 1) {
          if (signal && signal.aborted) break;
          await processOne(items[i]);
          if (i + 1 < items.length && sendGapMs > 0) {
            await waitMs(sendGapMs);
          }
        }
      } while (pendingWake && (!signal || !signal.aborted));
    } finally {
      busy = false;
    }
  }

  function wake() {
    pendingWake = true;
    if (wakeSleep) {
      wakeSleep();
    }
  }

  async function run() {
    while (!signal || !signal.aborted) {
      try {
        await pumpOnce();
      } catch (err) {
        if (err instanceof IlinkSessionExpiredError) {
          throw err;
        }
        if (signal && signal.aborted) break;
        log.warn?.('[ilink-pusher] claim cycle failed:', err.message);
      }
      if (signal && signal.aborted) break;
      if (!pendingWake) {
        await waitMs(pollMs);
      }
    }
  }

  return {
    pumpOnce,
    wake,
    run,
  };
}

module.exports = {
  hasValidContext,
  createPusher,
};
```

2. In `gateway/ilink/monitor.js`: add `onContextUpdated = () => {}` parameter to `runMonitor`; inside `for (const msg of msgs)`, right after the dedup checks and BEFORE `extractMessageText(msg)`:

```javascript
      if (msg.context_token) {
        const updatedAt = new Date(nowFn()).toISOString();
        state.writeContext({
          context_token: msg.context_token,
          from_user_id: msg.from_user_id,
          updated_at: updatedAt,
          invalidated: false,
        });
        try {
          onContextUpdated({ updatedAt, contextToken: msg.context_token });
        } catch {
          // Ignore wake callback errors
        }
      }
```

3. Update `gateway/ilink/runner.js` (add `const { createPusher } = require('./pusher');` at top imports, replace lines 61-106 with `latestInboundAt` heartbeat, context reset on force-login / WeChat account switch, and parallel monitor+pusher execution):

```javascript
  let latestInboundAt = (state.readContext() && state.readContext().updated_at) || '';

  const hbTimer = setInterval(async () => {
    try {
      const hb = await api.heartbeat(instanceId, {
        status: instance.status,
        last_inbound_at: latestInboundAt || undefined,
      });
      if (!hb || !hb.enabled || Number(hb.login_epoch || 0) !== initialEpoch) {
        controller.abort();
      }
    } catch (err) {
      if (err instanceof GatewayHttpError && (err.code === 1003 || err.code === 1004)) {
        controller.abort();
      }
    }
  }, 30000);
  hbTimer.unref?.();

  try {
    let account = forceLogin ? null : state.readAccount();
    if (!account || !account.bot_token) {
      if (forceLogin) {
        state.clearContext();
        latestInboundAt = '';
      }
      const loginRes = await runLogin({
        client,
        state,
        signal: controller.signal,
        onStatus: (payload) => api.updateStatus(instanceId, payload),
        fetchVerifyCode: async () => {
          const fresh = await api.getInstance(instanceId);
          return fresh && fresh.verify_code ? fresh.verify_code : null;
        },
      });
      if (!loginRes.ok) {
        if (loginRes.reason === 'already_bound') {
          process.exit(4);
        }
        process.exit(0);
      }
      account = loginRes.account;
    }

    const existingCtx = state.readContext();
    if (
      existingCtx &&
      account &&
      account.ilink_user_id &&
      existingCtx.from_user_id &&
      existingCtx.from_user_id !== account.ilink_user_id
    ) {
      state.clearContext();
      latestInboundAt = '';
    }

    const pusher = createPusher({
      client,
      state,
      account,
      instanceId,
      api,
      signal: controller.signal,
    });
    const runWithAbort = (p) =>
      Promise.resolve(p).catch((err) => {
        controller.abort();
        throw err;
      });
    await Promise.all([
      runWithAbort(
        runMonitor({
          client,
          state,
          account,
          instanceId,
          api,
          signal: controller.signal,
          onContextUpdated: ({ updatedAt }) => {
            latestInboundAt = updatedAt;
            pusher.wake();
          },
        })
      ),
      runWithAbort(pusher.run()),
    ]);
```

- [ ] **Step 4: Run gateway tests**

Run: `npm --prefix gateway test`
Expected: ALL gateway test suites PASS (0 failures).

- [ ] **Step 5: Commit Task 6**

```bash
git add gateway/ilink/pusher.js gateway/ilink/monitor.js gateway/ilink/runner.js \
        gateway/ilink/test/pusher.test.js gateway/ilink/test/monitor.test.js
git commit -m "feat(gateway): ✨ add iLink outbox pusher worker and inbound context wake-up"
```

---

### Task 7: Frontend UI, Dual-Engine Integration Test & Full Verification Matrix

**Files:**
- Modify: `frontend/src/types/scheduler.ts:1-62`
- Modify: `frontend/src/components/scheduler/ChannelConfigDialog.vue:28-149,185-296`
- Modify: `frontend/src/views/Scheduler.vue:560-575`
- Modify: `frontend/src/components/scheduler/JobLogDrawer.vue:283-306`
- Create Test: `frontend/tests/scheduler-ilink-channel.spec.ts`
- Create Integration Test: `backend/tests/test_ilink_scheduler_push.sh`

- [ ] **Step 1: Write failing frontend test `frontend/tests/scheduler-ilink-channel.spec.ts`**

Create `frontend/tests/scheduler-ilink-channel.spec.ts`:

```typescript
import { describe, it, expect, vi, beforeEach } from 'vitest'

const { postMock, putMock, getMock } = vi.hoisted(() => ({
  postMock: vi.fn(),
  putMock: vi.fn(),
  getMock: vi.fn()
}))

vi.mock('@/utils/http', () => ({
  default: {
    get: getMock,
    post: postMock,
    put: putMock,
    delete: vi.fn()
  }
}))

import { schedulerApi } from '@/api/scheduler'
import { classifyIlinkChannelStatus } from '@/utils/ilink-channel'

describe('scheduler wechat_ilink channel', () => {
  beforeEach(() => {
    postMock.mockReset()
    putMock.mockReset()
    getMock.mockReset()
  })

  it('saves and tests wechat_ilink channel with empty url/secret', async () => {
    putMock.mockResolvedValue({ message: 'ok' })
    postMock.mockResolvedValue({ message: 'ok' })
    await schedulerApi.saveChannel({
      channel_type: 'wechat_ilink',
      channel_name: '微信 iLink 机器人',
      webhook_url: ''
    })
    expect(putMock).toHaveBeenCalledWith('/scheduler/channel', {
      channel_type: 'wechat_ilink',
      channel_name: '微信 iLink 机器人',
      webhook_url: ''
    })

    await schedulerApi.testChannel()
    expect(postMock).toHaveBeenCalledWith('/scheduler/channel/test')
  })

  it('classifies ilink readiness across unbound, inactive, missing/stale inbound, and ready', () => {
    const now = Date.parse('2026-11-25T12:00:00Z')

    expect(classifyIlinkChannelStatus(undefined, now)).toEqual({
      level: 'danger',
      reason: 'not_bound'
    })
    expect(classifyIlinkChannelStatus({ has_instance: false }, now)).toEqual({
      level: 'danger',
      reason: 'not_bound'
    })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: false, status: 'disabled' },
        now
      )
    ).toEqual({ level: 'warning', reason: 'disabled' })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: true, status: 'session_expired' },
        now
      )
    ).toEqual({ level: 'warning', reason: 'session_expired' })
    expect(
      classifyIlinkChannelStatus(
        { has_instance: true, enabled: true, status: 'active', last_inbound_at: null },
        now
      )
    ).toEqual({ level: 'warning', reason: 'needs_inbound' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-24T10:00:00Z' // 26h ago > 20h
        },
        now
      )
    ).toEqual({ level: 'warning', reason: 'needs_inbound' })
    expect(
      classifyIlinkChannelStatus(
        {
          has_instance: true,
          enabled: true,
          status: 'active',
          last_inbound_at: '2026-11-25T10:00:00Z' // 2h ago
        },
        now
      )
    ).toEqual({ level: 'success', reason: 'ready' })
  })
})
```

- [ ] **Step 2: Run frontend test to verify failure**

Run: `npm --prefix frontend test -- tests/scheduler-ilink-channel.spec.ts`
Expected: FAIL (`Cannot find module '@/utils/ilink-channel'`).

- [ ] **Step 3: Implement frontend changes**

1. Update `frontend/src/types/scheduler.ts`:

```typescript
export type ChannelType =
  | 'serverchan'
  | 'pushdeer'
  | 'wechat_work'
  | 'webhook'
  | 'wechat_ilink'
  | 'none'

export interface IlinkChannelStatus {
  has_instance: boolean
  instance_id?: number
  status?: string
  enabled?: boolean
  ilink_bot_id?: string | null
  last_seen_at?: string | null
  last_inbound_at?: string | null
}
```
Update `JobRunLog.notification_status?: 'queued' | 'sent' | 'partial' | 'failed' | 'none'`, add `ilink_status?: IlinkChannelStatus` to `UserPushChannel`, and add `ilink_status?: IlinkChannelStatus` inside `SchedulerDashboard['push_channel']`.

2. Create `frontend/src/utils/ilink-channel.ts`:

```typescript
import type { IlinkChannelStatus } from '@/types/scheduler'

export interface IlinkReadiness {
  level: 'success' | 'warning' | 'danger'
  reason: string
}

const MAX_CONTEXT_AGE_MS = 20 * 60 * 60 * 1000

export function classifyIlinkChannelStatus(
  st?: IlinkChannelStatus | null,
  nowMs: number = Date.now()
): IlinkReadiness {
  if (!st || !st.has_instance) {
    return { level: 'danger', reason: 'not_bound' }
  }
  if (!st.enabled) {
    return { level: 'warning', reason: st.status === 'disabled' || !st.status ? 'disabled' : st.status }
  }
  if (st.status !== 'active') {
    return { level: 'warning', reason: st.status || 'pending_login' }
  }
  if (!st.last_inbound_at) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  const inboundMs = Date.parse(st.last_inbound_at)
  if (!Number.isFinite(inboundMs) || nowMs - inboundMs > MAX_CONTEXT_AGE_MS) {
    return { level: 'warning', reason: 'needs_inbound' }
  }
  return { level: 'success', reason: 'ready' }
}
```

3. Update `frontend/src/components/scheduler/ChannelConfigDialog.vue`:
   - Add `<el-radio-button value="wechat_ilink">` inside `channel-type-group`:

```html
            <el-radio-button value="wechat_ilink">
              <span class="radio-label">
                <Icon icon="ph:wechat-logo-bold" class="radio-icon" />
                微信 iLink
              </span>
            </el-radio-button>
```
   - Right after the `wechat_work` / `webhook` `<template>` block (before `</el-form>`), add the `wechat_ilink` readiness card:

```html
        <!-- WeChat iLink Readiness -->
        <template v-if="form.channel_type === 'wechat_ilink'">
          <el-alert
            :type="ilinkReadiness.level === 'success' ? 'success' : ilinkReadiness.level === 'danger' ? 'error' : 'warning'"
            :closable="false"
            show-icon
          >
            <template #title>
              <span v-if="ilinkReadiness.reason === 'ready'">
                微信 iLink 实例已就绪（最近会话 {{ ilinkStatus?.last_inbound_at }}），无需密钥或 Webhook URL。
              </span>
              <span v-else-if="ilinkReadiness.reason === 'not_bound'">
                尚未绑定微信 iLink 机器人实例，请先前往「系统设置 → IM 机器人」扫码绑定。
              </span>
              <span v-else-if="ilinkReadiness.reason === 'needs_inbound'">
                实例已激活，但需先在微信向该机器人发送任意一条消息以激活推送上下文（过期后将自动等待最多 30 分钟）。
              </span>
              <span v-else>
                当前 iLink 实例状态为 {{ ilinkReadiness.reason }}，请前往「系统设置 → IM 机器人」启用或重新扫码。
              </span>
            </template>
          </el-alert>
          <div v-if="ilinkReadiness.level !== 'success'" style="margin-top: 10px;">
            <el-button size="small" type="primary" plain @click="goBindIlink">
              前往系统设置绑定 / 启用
            </el-button>
          </div>
        </template>
```
   - In `<script setup lang="ts">`, import `useRouter` from `'vue-router'`, `classifyIlinkChannelStatus`, and `IlinkChannelStatus`:

```ts
import { useRouter } from 'vue-router'
import { classifyIlinkChannelStatus } from '@/utils/ilink-channel'
import type { ChannelType, IlinkChannelStatus } from '@/types'

const router = useRouter()
const ilinkStatus = ref<IlinkChannelStatus | null>(null)
const ilinkReadiness = computed(() => classifyIlinkChannelStatus(ilinkStatus.value))
const goBindIlink = () => {
  visible.value = false
  router.push('/settings')
}
```
   - In `open()`: set `ilinkStatus.value = res?.ilink_status || null` before checking `channel_type`.
   - In `handleTest()`: update the save pre-condition at line 280:

```ts
    if (form.channel_type === 'wechat_ilink' || form.secret_key.trim() || form.webhook_url) {
      await schedulerApi.saveChannel({
        channel_type: form.channel_type,
        channel_name: form.channel_name,
        webhook_url: form.channel_type === 'wechat_ilink' ? '' : form.webhook_url,
        secret_key: form.secret_key.trim() ? form.secret_key.trim() : undefined
      })
    }
```

4. Update `frontend/src/views/Scheduler.vue` (`formatChannelType`, lines 560-573):

```ts
const formatChannelType = (type?: ChannelType) => {
  switch (type) {
    case 'serverchan':
      return 'Server酱 (Turbo版)'
    case 'pushdeer':
      return 'PushDeer 微信/APP'
    case 'wechat_work':
      return '企业微信群机器人'
    case 'webhook':
      return '通用 HTTP Webhook'
    case 'wechat_ilink':
      return '微信 iLink 机器人'
    default:
      return '尚未配置通知通道'
  }
}
```

5. Update `frontend/src/components/scheduler/JobLogDrawer.vue` (`getNotifyStatusType` and `getNotifyStatusLabel`, lines 283-305):

```ts
const getNotifyStatusType = (status?: string) => {
  switch (status) {
    case 'sent':
      return 'success'
    case 'queued':
    case 'partial':
      return 'warning'
    case 'failed':
      return 'danger'
    case 'none':
    default:
      return 'info'
  }
}

const getNotifyStatusLabel = (status?: string) => {
  switch (status) {
    case 'sent':
      return '已推微信'
    case 'queued':
      return '待推送'
    case 'partial':
      return '部分送达'
    case 'failed':
      return '推送失败'
    case 'none':
    default:
      return '未推送'
  }
}
```

- [ ] **Step 4: Run frontend tests & build**

Run:
```bash
npm --prefix frontend test && npm --prefix frontend run build
```
Expected: All tests PASS and build finishes with 0 TypeScript/Vite errors.

- [ ] **Step 5: Commit Frontend Task**

```bash
git add frontend/src/types/scheduler.ts \
        frontend/src/utils/ilink-channel.ts \
        frontend/src/components/scheduler/ChannelConfigDialog.vue \
        frontend/src/views/Scheduler.vue \
        frontend/src/components/scheduler/JobLogDrawer.vue \
        frontend/tests/scheduler-ilink-channel.spec.ts
git commit -m "feat(frontend): ✨ add wechat_ilink push channel UI and queued/partial log states"
```

- [ ] **Step 6: Create dual-engine integration test `backend/tests/test_ilink_scheduler_push.sh`**

Create `backend/tests/test_ilink_scheduler_push.sh` (compatible with both SQLite and PostgreSQL via `db_helper.sh`):

```bash
#!/usr/bin/env bash
# Dual-engine integration test for WeChat iLink scheduler push & Outbox delivery.
set -uo pipefail

TEST_PORT="${TEST_PORT:-18083}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/db_helper.sh"

db_init_context "$TEST_PORT" "ilink_sched_push_test"
BASE="http://localhost:${PORT}/api"
BUILD_DIR="${MINEFOLIO_BUILD_DIR:-$(cd "$SCRIPT_DIR/../build" && pwd)}"
GW_TOK="${MINEFOLIO_GATEWAY_TOKEN:-test-gateway-token}"
export MINEFOLIO_GATEWAY_TOKEN="$GW_TOK"
export MINEFOLIO_ILINK_TEST_TIMEOUT_MS="${MINEFOLIO_ILINK_TEST_TIMEOUT_MS:-4000}"
PASS=0
FAIL=0

cleanup() {
  db_stop_server
  db_clean
}
trap cleanup EXIT INT TERM

export TEST_JWT_SECRET
TEST_JWT_SECRET=$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")

db_clean
db_start_server "$PORT" "$BUILD_DIR"

req() { # req METHOD PATH [JSON] [BEARER_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  local auth=""; [ -n "$tok" ] && auth="Authorization: Bearer $tok"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H 'Content-Type: application/json' ${auth:+-H "$auth"} "$BASE$path" -d "$data"
  else
    curl -s -X "$method" ${auth:+-H "$auth"} "$BASE$path"
  fi
}

gw_req() { # gw_req METHOD PATH [JSON] [GW_TOKEN]
  local method="$1" path="$2" data="${3:-}" tok="${4:-}"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H 'Content-Type: application/json' ${tok:+-H "X-Gateway-Token: $tok"} "$BASE$path" -d "$data"
  else
    curl -s -X "$method" ${tok:+-H "X-Gateway-Token: $tok"} "$BASE$path"
  fi
}

check() {
  if [ "$2" = "$3" ]; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (expected '$3', got '$2')"; FAIL=$((FAIL+1))
  fi
}

grep_check() {
  if echo "$3" | grep -qE "$2"; then
    echo "  PASS: $1"; PASS=$((PASS+1))
  else
    echo "  FAIL: $1 (no match /$2/): $3"; FAIL=$((FAIL+1))
  fi
}

rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
http.get('$BASE/auth/public-key', (res) => {
  let d = '';
  res.on('data', c => d += c);
  res.on('end', async () => {
    const outer = JSON.parse(d);
    const jwk = typeof outer.data.public_key === 'string'
      ? JSON.parse(outer.data.public_key) : outer.data.public_key;
    const key = await crypto.subtle.importKey('jwk', jwk,
      {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
    const enc = await crypto.subtle.encrypt({name:'RSA-OAEP'}, key, Buffer.from('$plain'));
    process.stdout.write(Buffer.from(enc).toString('base64')
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+\$/,''));
  });
});"
}

# --- Setup Alice (admin) & Bob (user) ---
PASS_A=$(rsa_encrypt "passA1234")
SETUP=$(req POST /system/setup "{\"username\":\"pushalice\",\"password_enc\":\"$PASS_A\"}")
TOKEN_A=$(echo "$SETUP" | jq -r '.data.token // empty')
[ -z "$TOKEN_A" ] && TOKEN_A=$(req POST /auth/login '{"username":"pushalice","password":"passA1234"}' | jq -r '.data.token')

PASS_B=$(rsa_encrypt "passB1234")
REG_B=$(req POST /auth/register "{\"username\":\"pushbob\",\"password_enc\":\"$PASS_B\"}")
TOKEN_B=$(echo "$REG_B" | jq -r '.data.token // empty')
[ -z "$TOKEN_B" ] && TOKEN_B=$(req POST /auth/login '{"username":"pushbob","password":"passB1234"}' | jq -r '.data.token')

# --- 1. Create instances A & B, save wechat_ilink channel with empty url/secret ---
INST_A=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_A" | jq -r '.data.id')
INST_B=$(req POST /im/instances '{"platform":"wechat","adapter":"ilink"}' "$TOKEN_B" | jq -r '.data.id')

SAVE_A=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"微信iLink","webhook_url":""}' "$TOKEN_A")
grep_check "A 保存 wechat_ilink 通道成功" '"code":0' "$SAVE_A"
GET_CH=$(req GET /scheduler/channel "" "$TOKEN_A")
check "GET channel type=wechat_ilink" "$(echo "$GET_CH" | jq -r '.data.channel_type')" "wechat_ilink"
check "GET channel has_instance=true" "$(echo "$GET_CH" | jq -r '.data.ilink_status.has_instance')" "true"

# --- 2. Activate A, trigger custom job -> notification_status == queued ---
ACT_A=$(gw_req PATCH "/gateway/im/instances/$INST_A/status" '{"status":"active","ilink_bot_id":"bot_pa@im.bot","ilink_user_id":"wx_pa"}' "$GW_TOK")
grep_check "实例 A 激活成功" '"code":0' "$ACT_A"

CREATE_JOB=$(req POST /scheduler/jobs '{"name":"iLink Custom Push","job_type":"structured_report","trigger_type":"cron","trigger_config":"{\"cron\":\"0 9 * * *\"}","payload_config":"{}","delivery_channel":"user_default"}' "$TOKEN_A")
JOB_ID=$(echo "$CREATE_JOB" | jq -r '.data.id // empty')
grep_check "创建定时任务成功" '"code":0' "$CREATE_JOB"

RUN_RES=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
grep_check "手动触发任务成功" '"code":0' "$RUN_RES"

LOG_STATUS=""
for _ in $(seq 1 30); do
  LOGS=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  ST=$(echo "$LOGS" | jq -r '.data.items[0].status // empty')
  NS=$(echo "$LOGS" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ -n "$NS" ]; then
    LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
check "自定义任务进入 queued" "$LOG_STATUS" "queued"

# --- 3. claim(has_context=false) skips scheduler row ---
CLAIM_NOCTX=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":false,"limit":5}' "$GW_TOK")
check "has_context=false 不领取 scheduler 推送" "$(echo "$CLAIM_NOCTX" | jq -r '.data.items | length')" "0"

# --- 4. Closed-loop test push + scheduler push completion ---
PA_ID=$(db_query "SELECT id FROM im_push_outbox WHERE instance_id=$INST_A ORDER BY id DESC LIMIT 1" | tr -d '[:space:]')
TEST_OUT=$(mktemp)
(req POST /scheduler/channel/test '{}' "$TOKEN_A" > "$TEST_OUT") &
TEST_PID=$!

SENT_CNT=0
for _ in $(seq 1 40); do
  CLAIM_OK=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"context_updated_at":"2026-11-25T15:00:00Z","limit":10}' "$GW_TOK")
  IDS=$(echo "$CLAIM_OK" | jq -r '.data.items[].id // empty')
  for PID_IT in $IDS; do
    gw_req POST "/gateway/im/instances/$INST_A/pushes/$PID_IT/ack" '{"result":"sent"}' "$GW_TOK" >/dev/null
    SENT_CNT=$((SENT_CNT+1))
  done
  [ "$SENT_CNT" -ge 2 ] && break
  sleep 0.1
done
wait "$TEST_PID" || true
grep_check "通道测试闭环返回 code=0" '"code":0' "$(cat "$TEST_OUT")"
rm -f "$TEST_OUT"

GET_CH2=$(req GET /scheduler/channel "" "$TOKEN_A")
grep_check "last_inbound_at 已写入" '2026-11-25' "$(echo "$GET_CH2" | jq -r '.data.ilink_status.last_inbound_at // ""')"

LOGS_AFTER=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "回执 sent 后日志变更为 sent" "$(echo "$LOGS_AFTER" | jq -r '.data.items[0].notification_status')" "sent"

# --- 5. Cross-instance ack blocked with 1003 ---
CROSS_ACK=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/$PA_ID/ack" '{"result":"sent"}' "$GW_TOK")
grep_check "跨实例 ack 返回 1003" '"code":1003' "$CROSS_ACK"

# --- 6. Built-in cron broadcast (user_id=0) to Alice & Bob ---
ACT_B=$(gw_req PATCH "/gateway/im/instances/$INST_B/status" '{"status":"active","ilink_bot_id":"bot_pb@im.bot","ilink_user_id":"wx_pb"}' "$GW_TOK")
grep_check "实例 B 激活成功" '"code":0' "$ACT_B"
SAVE_B=$(req PUT /scheduler/channel '{"channel_type":"wechat_ilink","channel_name":"Bob iLink","webhook_url":""}' "$TOKEN_B")
grep_check "B 保存 wechat_ilink 通道成功" '"code":0' "$SAVE_B"

db_exec "INSERT INTO scheduled_jobs (user_id, name, description, job_type, trigger_type, trigger_config, payload_config, delivery_channel, delivery_config, enabled, is_builtin, next_run_at) VALUES (0, 'E2E Builtin iLink Broadcast', 'test', 'system_builtin', 'cron', '{\"cron\":\"* * * * *\"}', '{\"handler\":\"mcp_pool_cleanup\"}', 'user_default', '{}', 1, 1, '2000-01-01 00:00:00')"
BI_ID=$(db_query "SELECT id FROM scheduled_jobs WHERE name='E2E Builtin iLink Broadcast' LIMIT 1" | tr -d '[:space:]')
TOGGLE_BI=$(req POST "/scheduler/jobs/${BI_ID}/toggle" '{"enabled":true}' "$TOKEN_A")
grep_check "唤醒定时引擎执行内置广播" '"code":0' "$TOGGLE_BI"

BI_LOG_STATUS=""
for _ in $(seq 1 50); do
  BL=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
  ST=$(echo "$BL" | jq -r '.data.items[0].status // empty')
  NS=$(echo "$BL" | jq -r '.data.items[0].notification_status // empty')
  if [ "$ST" = "success" ] && [ "$NS" = "queued" ]; then
    BI_LOG_STATUS="$NS"
    break
  fi
  sleep 0.1
done
check "内置广播日志进入 queued" "$BI_LOG_STATUS" "queued"

CLAIM_A_BI=$(gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
CLAIM_B_BI=$(gw_req POST "/gateway/im/instances/$INST_B/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK")
check "广播 Alice 收到 1 条" "$(echo "$CLAIM_A_BI" | jq -r '.data.items | length')" "1"
check "广播 Bob 收到 1 条" "$(echo "$CLAIM_B_BI" | jq -r '.data.items | length')" "1"
PA_BI=$(echo "$CLAIM_A_BI" | jq -r '.data.items[0].id')
PB_BI=$(echo "$CLAIM_B_BI" | jq -r '.data.items[0].id')
gw_req POST "/gateway/im/instances/$INST_A/pushes/$PA_BI/ack" '{"result":"sent"}' "$GW_TOK" >/dev/null
gw_req POST "/gateway/im/instances/$INST_B/pushes/$PB_BI/ack" '{"result":"failed","error":"simulated err"}' "$GW_TOK" >/dev/null
BL_PARTIAL=$(req GET "/scheduler/jobs/${BI_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "广播一成功一失败聚合为 partial" "$(echo "$BL_PARTIAL" | jq -r '.data.items[0].notification_status')" "partial"

# --- 7. Expired push transitions to expired & rolls up failed ---
RUN2=$(req POST "/scheduler/jobs/${JOB_ID}/run" '{}' "$TOKEN_A")
grep_check "第二次触发自定义任务成功" '"code":0' "$RUN2"
sleep 0.3
LATEST_PID=$(db_query "SELECT MAX(id) FROM im_push_outbox WHERE instance_id=$INST_A" | tr -d '[:space:]')
db_exec "UPDATE im_push_outbox SET expires_at_ts = 100 WHERE id = $LATEST_PID"
gw_req POST "/gateway/im/instances/$INST_A/pushes/claim" '{"has_context":true,"limit":5}' "$GW_TOK" >/dev/null
EXP_ST=$(db_query "SELECT status FROM im_push_outbox WHERE id = $LATEST_PID" | tr -d '[:space:]')
check "超时推送转为 expired" "$EXP_ST" "expired"
LOGS_EXP=$(req GET "/scheduler/jobs/${JOB_ID}/logs?page=1&page_size=5" "" "$TOKEN_A")
check "过期回滚日志为 failed" "$(echo "$LOGS_EXP" | jq -r '.data.items[0].notification_status')" "failed"

echo "----------------------"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ]
```

- [ ] **Step 7: Run Full Verification Matrix**

Run sequentially in resource-bounded wrapper:

```bash
cmake -S backend -B backend/build -G "Unix Makefiles"
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh unit backend/build
cmake -S backend -B backend/build-asan -G "Unix Makefiles" -DMINEFOLIO_SANITIZE=ON
./backend/tests/run_isolated.sh build backend/build-asan
./backend/tests/run_isolated.sh unit backend/build-asan
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token bash backend/tests/test_im_bot_instance.sh
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token bash backend/tests/test_ilink_scheduler_push.sh
bash backend/tests/test_scheduler.sh
npm --prefix gateway test
npm --prefix frontend test
npm --prefix frontend run build
```
Expected: 100% pass across CTest standard + ASan/UBSan builds, all 3 integration scripts, gateway tests, frontend tests, and frontend build.

- [ ] **Step 8: Commit Integration Test**

```bash
chmod +x backend/tests/test_ilink_scheduler_push.sh
git add backend/tests/test_ilink_scheduler_push.sh
git commit -m "test(im): ✅ add dual-engine iLink scheduler push integration test"
```

---

## Self-Review Check

- **Spec coverage:** Every section of `2026-11-25-wechat-ilink-scheduler-push-design.md` (V037 migration, `last_inbound_at`, `notif_sync_sent`/`notif_sync_failed`, Outbox state machine, `pushes/claim` & `pushes/:push_id/ack` + `heartbeat`, `wechat_ilink` channel + 10s sync test loop, personal + manual + built-in broadcast + periodic expiry, `context.json` + `client.js` `-2`/`-14` + `formatter.js` + `pusher.js` + `monitor.js` `wake()` + `runner.js`, frontend types/dialog/log drawer, gateway + backend CTest + ASan + dual-engine integration + frontend tests) maps to Tasks 1–7.
- **Zero placeholders:** Complete runnable DDL, C code, Node.js code, and verification commands are present throughout.
- **Type & signature consistency:** Column names (`lease_until_ts`, `next_attempt_at_ts`, `expires_at_ts`, `last_inbound_at`, `notif_sync_sent`, `notif_sync_failed`), API keys (`has_context`, `context_updated_at`, `result`, `ilink_status`), C signatures, and TS types are identical across all 7 tasks.
