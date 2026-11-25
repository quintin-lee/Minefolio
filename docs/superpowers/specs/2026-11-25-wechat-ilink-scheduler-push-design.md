# 微信 iLink 接收定时任务推送设计

**Date:** 2026-11-25
**Status:** Approved (design), pending spec review
**Scope:** Minefolio backend + gateway + frontend

## 1. Goal

让已绑定个人微信 iLink 独立机器人实例（`im_bot_instances`）的 Minefolio 用户，能够将微信 iLink 作为调度中心的默认推送通道之一，实时接收各类定时任务产物，同时保持现有网关单向通信与凭证不出网关的安全边界。

覆盖范围：

- 在用户通知通道配置（`user_notification_channels`）中新增 `channel_type = 'wechat_ilink'`，与 `serverchan`、`pushdeer`、`wechat_work`、`webhook` 并列，复用「默认推送通道（`user_default`）」与「发送测试消息」闭环。
- 支持个人自建定时任务（`user_id > 0`）、调度中心手动「立即执行」，以及系统内置全局广播任务（`user_id == 0` 且 `delivery_channel != 'silent_db'`，如「AI 每日金融快讯提炼」）。
- 通过后端持久化 Outbox 队列与 30 分钟有效窗口（TTL `1800s`），支持网关短时离线恢复后自动补推，以及用户后续在微信向机器人发送消息刷新 `context_token` 后的即时唤醒补推。
- 在前端展示 iLink 通道就绪状态（是否已绑定、是否已激活会话 `last_inbound_at`）与任务日志推送态（`queued` / `partial` / `sent` / `failed`）。

非目标（本迭代不做）：

- 将 `bot_token` 或 `context_token` 存入后端数据库、写入后端日志或返回给前端。
- 在网关侧开放入站 HTTP 监听端口或配置后端到网关的反向路由（与 SSRF 白名单）。
- 单任务级别的多通道分发矩阵（`scheduled_jobs.delivery_channel` 仍保持 `user_default` / `custom_webhook` / `silent_db`）。
- 富媒体推送（图片、卡片、文件、群聊广播）。
- 改动 `ai_news_scheduler_tick()` 等内置任务的内容生成逻辑。

## 2. 核心安全不变量

1. **凭证绝对不出网关**：`bot_token` 与 `context_token` 永不离开 Node 网关状态目录（`<ILINK_STATE_DIR>/<instance_id>/account.json` 与 `context.json`，目录权限 `0700`，文件权限 `0600`），不写日志、不入后端 DB、不传前端。后端 `im_bot_instances` 仅持久化脱敏时间戳 `last_inbound_at`。
2. **保持单向通信与服务鉴权**：延续网关主动连后端架构；新增 `/api/gateway/im/instances/:id/pushes/*` 端点统一通过 `gateway_auth_check()` 对 `X-Gateway-Token` 做常量时间校验，未配置 `MINEFOLIO_GATEWAY_TOKEN` 时一律拒绝（`1001`）。
3. **严格实例归属校验**：`claim` 与 `ack` 接口强制校验 `im_push_outbox.instance_id = :id` 且实例满足 `enabled = 1` 与 `status = 'active'`；跨实例 `ack` 返回 `1003`，不泄露其他实例的任务存在性。
4. **投递目标锁定 owner**：网关发送 `POST /ilink/bot/sendmessage` 时，`to_user_id` 强制取本地 `account.ilink_user_id`（并在 `state.js` 启动及收到消息时校验一致性），不允许调度任务或请求体覆盖为任意微信用户 ID。
5. **纯文本降噪**：所有调度产出通过纯函数 `formatter.js` 转为微信可读纯文本后按分片发送，不执行 Markdown 内嵌脚本或扩展协议指令。

## 3. 总体架构与数据流

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                        Minefolio Backend (C23)                           │
│                                                                          │
│  [scheduler_engine.c]                                                    │
│    ├─ user_id > 0 (个人任务 / 手动触发) ──► 用户默认通道                   │
│    └─ user_id == 0 且 != silent_db (内置广播) ──► 遍历所有激活用户通道      │
│          │                                                               │
│          ├─ serverchan / pushdeer / wechat_work / webhook ─► HTTP 同步推  │
│          └─ wechat_ilink ──► im_push_usecase_enqueue()                   │
│                                     │                                    │
│  [scheduler/usecases.c]             ▼                                    │
│    └─ 测试通道 (kind='test') ─► im_push_outbox (DB 持久化队列) ◄── TTL 巡检│
│                                     ▲                                    │
└─────────────────────────────────────┼────────────────────────────────────┘
                                      │ X-Gateway-Token (单向 Pull / Ack)
                                      │ POST .../pushes/claim
                                      │ POST .../pushes/:push_id/ack
┌─────────────────────────────────────┴────────────────────────────────────┐
│                Node Gateway: ilink-runner (每实例独立子进程)                │
│                                                                          │
│  [monitor.js] 收到 owner 入站消息                                          │
│    ├─► state.writeContext(context.json, 0600)                             │
│    └─► onContextUpdated() ──► [pusher.js].wake() 立即触发 claim 补推      │
│                                                                          │
│  [pusher.js] 每 3s 轮询 claim (或被 wake() 唤醒)                          │
│    ├─ has_context=false ─► 仅领 kind='test' 回执失败；保留 scheduler 待唤醒 │
│    └─ has_context=true  ─► formatter.js 清洗与 1800 字分片                │
│                              └─► client.sendText() ─► iLink sendmessage  │
│                                                        ──► owner 个人微信  │
└──────────────────────────────────────────────────────────────────────────┘
```

## 4. 数据模型（`V037__im_push_outbox.sql`）

新增双引擎迁移脚本：
- `backend/sql/migrations/sqlite/V037__im_push_outbox.sql`
- `backend/sql/migrations/postgres/V037__im_push_outbox.sql`

并同步更新全量建表基线 `backend/sql/migration.sql` 与 `backend/sql/migration_postgres.sql`。

### 4.1 既有表扩展

1. **`user_notification_channels`**：
   - `channel_type` 新增合法枚举值 `'wechat_ilink'`。
   - 保存 `'wechat_ilink'` 时，`webhook_url` 与 `secret_key` 允许为空字符串（不校验 SENDKEY/URL）。
2. **`im_bot_instances`**：
   - 新增可空列 `last_inbound_at`（SQLite：`TEXT`；PostgreSQL：`TIMESTAMPTZ`），记录网关最近一次收到该实例 owner 入站消息的时间戳（由网关在 `heartbeat` 或 `pushes/claim` 时上报，不含 `context_token`）。
3. **`job_run_logs`**：
   - `notification_status` 取值由 `'none' | 'sent' | 'failed'` 扩展为 `'none' | 'queued' | 'sent' | 'partial' | 'failed'`。
   - 新增整数计数列 `notif_sync_sent`（`DEFAULT 0`）与 `notif_sync_failed`（`DEFAULT 0`），用于内置广播任务混合投递同步通道（Server酱 / PushDeer / 企微 / Webhook）与异步 Outbox（`wechat_ilink`）时，支持无字符串解析的精确状态聚合，确保零报错时 `error_message` 保持为空。

### 4.2 `im_push_outbox`（SQLite）

```sql
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

### 4.3 `im_push_outbox`（PostgreSQL）

```sql
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

**字段语义约定：**

- `lease_until_ts`、`next_attempt_at_ts`、`expires_at_ts` 统一使用 UTC Unix 秒（`INTEGER` / `BIGINT`），保证 SQLite 与 PostgreSQL 的过期判断、租约回收及退避时间比较逻辑完全一致。
- `kind`：`'scheduler'`（定时/手动任务推送，TTL 默认 `1800` 秒）或 `'test'`（通道连通性测试，TTL `30` 秒）。
- `status` 状态迁移：`pending` → `leased` → `sent` | `failed` | `expired`（其中 `waiting_context` 或可重试失败可由 `leased` 回到 `pending`）。

## 5. 后端接口与业务逻辑设计

响应格式统一 `{code, message, data}`。

### 5.1 网关接口（`X-Gateway-Token`，挂载在 `im_bot_instance_controller.c`）

#### `POST /api/gateway/im/instances/:id/pushes/claim`

请求体：

```json
{
  "has_context": true,
  "context_updated_at": "2026-11-25T14:30:00Z",
  "limit": 5
}
```

行为：

1. 校验实例存在；若 `enabled != 1` 或 `status != 'active'`，返回 `1004`。
2. `limit` 默认 `5`，钳制到 `[1, 10]`。
3. 若提供非空合法的 `context_updated_at`，更新该实例的 `im_bot_instances.last_inbound_at`（同时刷新 `last_seen_at`）。
4. 在事务内按 `now = time(NULL)` 对归属本实例的队列记录执行状态整理：
   - 将本实例下 `status IN ('pending', 'leased') AND expires_at_ts <= now` 的记录置为 `status = 'expired'`、`last_error = '超过30分钟等待窗口（网关离线或微信会话未激活），推送已过期'`，并收集关联的 `job_run_log_id` 进行状态回写。
   - 将本实例下 `status = 'leased' AND lease_until_ts <= now` 的记录回收为 `status = 'pending'`、`lease_until_ts = 0`（防止 runner 异常崩溃后永久卡死）。
5. 领取候选记录（`instance_id = :id AND status = 'pending' AND next_attempt_at_ts <= now AND expires_at_ts > now`）：
   - **`has_context == false`**：只筛选 `kind = 'test'`；将 `kind = 'scheduler'` 留在 `pending`（不递增 `attempts`，不占用退避），等待后续用户发消息唤醒。
   - **`has_context == true`**：筛选全部 `kind`，排序优先级 `CASE WHEN kind = 'test' THEN 0 ELSE 1 END, id ASC`，上限 `limit`。
6. 将选中记录原子更新为 `status = 'leased'`、`lease_until_ts = now + 60`（60 秒租约）。
7. 返回：

```json
{
  "items": [
    {
      "id": 42,
      "kind": "scheduler",
      "title": "每日早晨持仓早报",
      "content": "### ...",
      "attempts": 0,
      "max_attempts": 5
    }
  ]
}
```

#### `POST /api/gateway/im/instances/:id/pushes/:push_id/ack`

请求体：

```json
{
  "result": "sent",
  "error": "",
  "retry_after_sec": 0
}
```

允许的 `result` 枚举：`"sent"`、`"retry"`、`"waiting_context"`、`"failed"`。

行为（事务内）：

1. 按 `id = push_id AND instance_id = id` 查询记录；不存在或非本实例返回 `1003`；若已处于终态（`sent` / `failed` / `expired`）则幂等返回 `ok`。
2. 分支处理：
   - **`sent`**：`status = 'sent'`、`lease_until_ts = 0`、`last_error = ''`。
   - **`waiting_context`**：仅用于无有效 `context_token`（如 `-2` 失效或 `has_context == false`）：
     - 若 `kind == 'test'` → 立即转终态 `status = 'failed'`、`last_error = (error 或 "请先在微信向机器人发送任意一条消息以建立会话，再重新测试")`。
     - 若 `kind == 'scheduler'` → 若 `expires_at_ts <= now` 则转 `'expired'`；否则退回 `status = 'pending'`、`lease_until_ts = 0`、`next_attempt_at_ts = now + 30`、**不递增 `attempts`**，`im_push_outbox.last_error` 记录 `"等待微信会话上下文刷新（请在微信给机器人发送任意消息）"`，且保持 `im_bot_instances.last_error` 不变。
   - **`retry`**：`attempts = attempts + 1`、`lease_until_ts = 0`；
     - 若 `kind == 'test'` → 直接转终态 `'failed'`；
     - 否则若 `expires_at_ts <= now && attempts < max_attempts` → 转终态 `'expired'`；
     - 否则若 `attempts >= max_attempts` → 转终态 `'failed'`；
     - 否则回到 `'pending'`，`next_attempt_at_ts = now + backoff`（退避表 `[10, 30, 60, 120]` 秒，若传合法 `retry_after_sec` 则按 `min(retry_after_sec, 300)`）。
   - **`failed`**：直接转终态 `status = 'failed'`、`lease_until_ts = 0`、`last_error = error`。
3. 若该记录关联了 `job_run_log_id > 0`，调用 `im_push_rollup_job_log(pool, job_run_log_id)` 聚合更新 `job_run_logs.notification_status` 与 `error_message`。

#### `POST /api/gateway/im/instances/:id/heartbeat`（扩展）

- 请求体可选新增 `{ "status": "active", "last_inbound_at": "2026-11-25T14:30:00Z" }`。
- 若提供 `last_inbound_at`，在 `mf_im_bot_instance_heartbeat` 中顺带更新 `im_bot_instances.last_inbound_at`（空串或未传则保持原值）。

### 5.2 Owner / 调度器 API（JWT + CSRF）

#### `GET /api/scheduler/channel` 与 `GET /api/scheduler/dashboard`

- 当用户存在（或未创建）iLink 实例时，在响应 `push_channel` 对象内统一附带只读状态字段 `ilink_status`（不含任何密钥或 token）：

```json
{
  "has_instance": true,
  "instance_id": 1,
  "status": "active",
  "enabled": true,
  "ilink_bot_id": "bot_xxx",
  "last_seen_at": "2026-11-25T14:35:00Z",
  "last_inbound_at": "2026-11-25T14:20:00Z"
}
```

#### `PUT /api/scheduler/channel`

- `scheduler_usecase_save_channel()` 支持 `channel_type == 'wechat_ilink'`：
  - 对 `channel_type` 做白名单校验（`serverchan` / `pushdeer` / `wechat_work` / `wechatwork` / `qywx` / `webhook` / `wechat_ilink`），非法类型返回 `1002`。
  - `wechat_ilink` 允许空 `webhook_url` 与空 `secret_key`；不清空用户历史保存在通道中的其他字段逻辑保持兼容。

#### `POST /api/scheduler/channel/test`

当用户当前启用通道的 `channel_type == 'wechat_ilink'` 时，`scheduler_usecase_test_channel()` 执行同步闭环：

1. 查询用户 `(wechat, ilink)` 实例：
   - 不存在 → 失败，提示`"您尚未创建微信 iLink 机器人实例，请先前往「设置 → IM 机器人」绑定"`。
   - `enabled != 1` → 失败，提示`"微信 iLink 机器人已停用，请先启用实例"`。
   - `status != 'active'` → 失败，按状态返回具体提示（如登录态已过期需重新扫码 / 等待扫码登录完成）。
2. 插入一条 `im_push_outbox` 记录：`kind = 'test'`、`job_run_log_id = NULL`、`ttl = 30s`、`title = "Minefolio 推送连通性测试"`、`content = test_msg`。
3. 同步短轮询该 `push_id` 状态（每 `200ms` 查询一次，最长 `10s`）：
   - `status == 'sent'` → 返回 `0`（成功，前端显示已送达）。
   - `status == 'failed' || status == 'expired'` → 返回 `-1`，并将 `last_error` 原样填入 `err_buf` 返回前端（例如`"请先在微信向机器人发送任意一条消息以建立会话，再重新测试"`）。
   - 超过 `10s` 仍为 `pending` / `leased` → 将该测试记录更新为 `expired`（避免网关稍后上线补发一条过时测试消息），返回 `-1` 与错误 `"iLink 网关未在 10 秒内响应，请检查 ilink-server 进程是否运行且网络可达"`。

### 5.3 调度引擎分发、内置全局任务广播与过期巡检（`scheduler_engine.c`）

1. **提取单用户投递辅助函数 `deliver_to_user_channel(pool, user_id, log_id, title, markdown_body, out_status, err_buf, err_cap)`**：
   - 若用户无活跃通道（`is_active != 1` 或 `channel_type == 'none'`）→ `out_status = "none"`。
   - 若 `channel_type == 'wechat_ilink'`：
     - 校验用户存在 `enabled = 1 && status = 'active'` 的 iLink 实例；若满足则调用 `im_push_usecase_enqueue(..., ttl=1800)` 入队，返回 `out_status = "queued"`；
     - 若不存在或未激活/已过期，不空等 30 分钟，直接记录具体错误并令 `out_status = "failed"`。
   - 其余通道（`serverchan` / `pushdeer` / `wechat_work` / `webhook`）→ 保持现有 `wechat_push_service_send()` 同步投递逻辑不变。
2. **个人任务与手动触发**：
   - 当 `job.user_id > 0` 或 `ctx->user_id > 0`（用户在界面对内置任务点击「立即执行」）且 `delivery_channel != 'silent_db'`：仅投递给目标 `user_id`。
   - 注：针对 `scheduler_usecase_job_run_now()` 触发 `user_id = 0` 的系统内置任务，`ctx->user_id` 即为触发管理员本人 ID，因此走单用户投递不误广播全部用户。
3. **内置全局任务定时触发广播（`job_user_id == 0 && ctx->user_id == 0 && delivery_channel != 'silent_db'`，如「AI 每日金融快讯提炼」）**：
   - 查询 `mf_scheduler_repo_list_active_channels(pool)` 获取所有 `is_active = 1` 的用户通道配置。
   - 逐一投递并关联同一个 `job_run_logs.id`，并记录 `notif_sync_sent` / `notif_sync_failed` 与同步通道首条错误摘要：
     - 若至少有一条 `wechat_ilink` 成功入队，初态暂记 `notification_status = 'queued'`，待 Outbox 全数终态后再聚合更新 `job_run_logs`。
     - 若无 `wechat_ilink` 入队（纯同步通道或含失败的 iLink 配置），直接聚合：`既有成功又有失败 → partial`、`全成功 → sent`、`全失败 → failed`、`无活跃通道 → none`。
4. **多通道聚合回写规则（`im_push_rollup_job_log()`）**：
   - 结合 `job_run_logs.(notif_sync_sent, notif_sync_failed)` 与 `job_run_log_id = :lid` 的全部 `im_push_outbox` 记录统计 `ok_total = notif_sync_sent + outbox_sent`、`fail_total = notif_sync_failed + outbox_failed_or_expired`：
     - 存在 `status IN ('pending', 'leased')` 的 Outbox 记录 → `notification_status = 'queued'`。
     - 全部 Outbox 记录已进入终态：
       - 若 `ok_total > 0 && fail_total > 0` → `'partial'`；
       - 若 `ok_total > 0 && fail_total == 0` → `'sent'`；
       - 若 `ok_total == 0 && fail_total > 0` → `'failed'`（`error_message` 保留同步或异步首条非空错误及失败计数）。
5. **过期巡检**：
   - `scheduler_engine_thread` 每轮调度周期末调用 `im_push_usecase_expire_stale(s_db_pool)`，即使所有网关进程完全离线，也能按时将超过 30 分钟的 `im_push_outbox` 记录转为 `expired`，并回写关联 `job_run_logs` 为 `failed`（或 `partial`），防止面板永久停留在「待推送」。

### 5.4 后端 DDD 文件清单

| 层 | 文件 | 职责 |
|---|---|---|
| 迁移与 Schema | `backend/sql/migrations/{sqlite,postgres}/V037__im_push_outbox.sql`、`backend/sql/migration{,_postgres}.sql` | `last_inbound_at` 列与 `im_push_outbox` 表及索引 |
| 仓储（推送队列） | `backend/src/infrastructure/repositories/im_push_outbox_repo_impl.{h,c}` | 入队、原子领租约、回执状态推进、过期清理、日志聚合查询（双引擎 SQL） |
| 仓储（IM 实例） | `backend/src/infrastructure/repositories/im_bot_repo_impl.{h,c}` | 增加 `mf_im_bot_instance_get_active_by_user()`、心跳/状态更新支持 `last_inbound_at` |
| 仓储（调度） | `backend/src/infrastructure/repositories/scheduler_repo_impl.{h,c}` | 增加 `mf_scheduler_repo_list_active_channels()`、`mf_scheduler_repo_log_update_notif()` |
| 领域契约 | `backend/src/domain/scheduler/entity.h`、`backend/src/domain/scheduler/repository.h` | 同步通知状态注释与上述调度仓储函数契约 |
| 用例（推送） | `backend/src/application/ai/im_push_usecases.{h,c}` | `enqueue`、`gateway_claim`、`gateway_ack`、`expire_stale`、`get_status`（`void* pool` 签名） |
| 用例（IM/调度） | `backend/src/application/ai/im_bot_instance_usecases.{h,c}`、`backend/src/application/scheduler/usecases.c` | 心跳支持 `last_inbound_at`；通道保存兼容 `wechat_ilink`、返回 `ilink_status`、测试通道同步轮询 |
| 控制器 | `backend/src/interfaces/http/controllers/im_bot_instance_controller.c` | 注册并处理 `pushes/claim`、`pushes/:push_id/ack` 与扩展 `heartbeat` |
| 调度引擎 | `backend/src/core/scheduler/scheduler_engine.c` | 单用户与全局广播分发、同步+异步结果聚合、周期过期扫描 |

## 6. 网关设计（`gateway/ilink/`）

### 6.1 文件结构与职责

| 文件 | 变更 | 职责 |
|---|---|---|
| `gateway/ilink/state.js` | 修改 | 每实例目录增加 `context.json`（`readContext`/`writeContext`/`clearContext`），`0600` 原子写 |
| `gateway/ilink/client.js` | 修改 | 新增 `IlinkContextInvalidError`；`sendText` 校验返回体非 0 `ret`/`errcode` 并精确分类 |
| `gateway/ilink/formatter.js` | 新增 | Markdown 转微信纯文本 + 自然换行优先分片纯函数 |
| `gateway/ilink/monitor.js` | 修改 | 每次收到 owner 入站消息即更新 `context.json` 并回调 `onContextUpdated()` |
| `gateway/ilink/pusher.js` | 新增 | 独立 Outbox 拉取/唤醒/发送/确认循环，限流间隔与错误分类回执 |
| `gateway/ilink/api.js` | 修改 | 新增 `claimPushes()`、`ackPush()`；`heartbeat()` 支持可选 `last_inbound_at` |
| `gateway/ilink/runner.js` | 修改 | 并行启动 `runMonitor` 与 `runPusher`，串联 `wake()` 与重新登录时清理旧 `context.json` |

### 6.2 配置环境变量

| 变量 | 默认值 | 说明 |
|---|---|---|
| `ILINK_PUSH_POLL_MS` | `3000` | Outbox 拉取周期（毫秒） |
| `ILINK_PUSH_BATCH_LIMIT` | `5` | 单次 `claim` 最大条数 |
| `ILINK_PUSH_SEND_GAP_MS` | `500` | 多条消息之间及长消息分片之间的发送间隔（毫秒），防止触发微信限流 |
| `ILINK_REPLY_CHUNK` | `1800` | 复用已有环境变量控制推送消息单片最大字符数 |

### 6.3 `context.json` 持久化与会话校验（`state.js` / `monitor.js` / `runner.js`）

- 文件路径：`<ILINK_STATE_DIR>/<instance_id>/context.json`（`0600`）。
- 内容结构：

```json
{
  "context_token": "opaque_token_from_getupdates",
  "from_user_id": "wxid_owner",
  "updated_at": "2026-11-25T14:30:00.000Z",
  "invalidated": false
}
```

- 生命周期规则：
  1. `runner.js` 启动拿到 `account` 后，若已存在 `context.json` 且 `context.from_user_id !== account.ilink_user_id`，立即执行 `state.clearContext()`（切换绑定的微信账号时不混用旧上下文）。
  2. 执行 `--force-login` 重新登录后，强制清空旧 `context.json`。
  3. `monitor.js` 在收到 `from_user_id === account.ilink_user_id`（且非机器人自身 `message_type === 2`、非群聊）的任意入站消息时，**在类型过滤之前**（无论用户发送文字、语音还是图片表情，只要含非空 `msg.context_token`）立即调用 `state.writeContext(...)`（置 `invalidated: false`），并触发 `onContextUpdated({ updatedAt })`。
  4. `onContextUpdated` 唤醒 `pusher.wake()`（立即发起一次 `claim`）并将 `updatedAt` 传给最近一次心跳记录。

### 6.4 错误分类（`client.js`）

现有 `client.js` 在 `requestJson()` 中仅检查 `ret === -14 || errcode === -14`，对 HTTP 200 下返回的其他非 0 `ret`/`errcode` 未抛错。本设计完善以下分类：

- 新增错误类 `IlinkContextInvalidError`（上下文不可用或失效）。
- 当响应 JSON 中存在数字字段 `ret !== 0` 或 `errcode !== 0` 时（且两者非均为空/0）：
  - 若为 `-14` → 抛出 `IlinkSessionExpiredError`。
  - 若为 `-2`，或错误信息明确指示 context/token 无效（如匹配 `context_token` invalid/expired）→ 抛出 `IlinkContextInvalidError`。
  - 其余非 0 错误 → 抛出 `Error('iLink error <code>: <errmsg>')`。

### 6.5 Markdown 排版与自然断行分片（`formatter.js`）

调度器产出的内容格式为标准 Markdown，直接发给微信会出现大量 `###`、`**`、`> ` 与表格线噪声。`formatter.js` 提供纯函数：

1. **`formatPushText(title, markdownBody)`**：
   - 将 `[文字](https://...)` 转换为 `文字 (https://...)`。
   - 去除代码块围栏 `` ```lang ``、行内反引号 `` ` ``、标题井号 `^#{1,6}\s*`、引用前缀 `^>\s*`、粗斜体 `**` / `__`、水平分隔线 `^---+$`；将 Markdown 表格分隔行 `|---|` 删除并将数据行 `| col1 | col2 |` 整理为 `col1 · col2`。
   - 保留无序列表符号 `- ` / `• `，折叠连续 3 个及以上换行为 2 个换行。
   - 若清洗后正文未以 `title` 开头且 `title` 非空，则在顶部加上 `【${title}】\n\n`。
2. **`splitPushChunks(text, chunkSize)`**：
   - 上限取 `chunkSize`（默认 `1800`）。
   - 每片切分时优先在 `(0.6 * chunkSize, chunkSize]` 区间内回溯最后一个 `\n`，保证不把一句话或数据行腰斩；若整段无换行则硬切。

### 6.6 `pusher.js` 运行循环与并发协作

```text
wake() 或每 3000ms 到期
   │
   ├─ 读取 context = state.readContext()
   ├─ hasContext = Boolean(context && context.context_token && !context.invalidated)
   ├─ api.claimPushes(instanceId, { has_context: hasContext, context_updated_at, limit: 5 })
   │
   └─ 逐条串行处理 items:
        ├─ 若本地无有效 context:
        │    └─ api.ackPush(item.id, { result: 'waiting_context', error: '请先在微信向机器人发送任意一条消息以建立会话' })
        └─ 若有有效 context:
             ├─ fragments = splitPushChunks(formatPushText(item.title, item.content))
             ├─ 逐片发送（每片失败最多原地重试 1 次，片间 sleep 500ms）
             ├─ 全部成功 ─► api.ackPush(item.id, { result: 'sent' })
             ├─ IlinkSessionExpiredError (-14) ─►
             │    上报 status='session_expired'，ackPush('retry' / test→'failed')，抛出终止 runner (exit 2)
             ├─ IlinkContextInvalidError (-2) ─►
             │    写回 context.invalidated = true，ackPush({ result: 'waiting_context', error: ... })
             └─ 网络/超时/5xx 临时错误 ─►
                  ackPush({ result: 'retry', error: err.message })
```

并发协作细节：

- `runner.js` 使用 `Promise.all([runMonitor(...), runPusher(...)])` 并在任一抛出 `IlinkSessionExpiredError` 时 abort 共享 `AbortController`，统一以退出码 `2` 退出（supervisor 按已有逻辑不重启，等待 App 重新扫码登录）。
- `monitor.js` 回复日常对话与 `pusher.js` 发送定时推送共用各自的 `sendText` 请求，节点间隔 `500ms` 控制发送节奏；收到 `wake()` 信号时若当前正在发消息，则置位 `pendingWake = true`，本轮发送完成后立即补查一次不丢失唤醒信号。

## 7. 前端设计

### 7.1 类型扩展（`frontend/src/types/scheduler.ts`）

- `ChannelType` 新增 `'wechat_ilink'`：
  `export type ChannelType = 'serverchan' | 'pushdeer' | 'wechat_work' | 'webhook' | 'wechat_ilink' | 'none'`
- `JobRunLog.notification_status` 扩展：
  `notification_status?: 'queued' | 'sent' | 'partial' | 'failed' | 'none'`
- 新增接口 `IlinkChannelStatus`：

```ts
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

并在 `UserPushChannel` 与 `SchedulerDashboard.push_channel` 中挂载可选字段 `ilink_status?: IlinkChannelStatus`。

### 7.2 通道配置弹窗（`frontend/src/components/scheduler/ChannelConfigDialog.vue`）

1. 在 `el-radio-group` 中增加第 5 个选项：**微信 iLink 机器人**（`value="wechat_ilink"`，与 Server酱等并列）。
2. 选择 `wechat_ilink` 时隐藏 `secret_key` / `webhook_url` 输入框，渲染「iLink 机器人就绪状态面板」：
   - **未创建 / 未在线**（`!has_instance || status !== 'active' || !enabled`）：显示当前实例状态（未创建 / 等待扫码 / 已停用 / 登录态过期）及按钮「前往设置绑定微信机器人」（关闭弹窗后跳转至 `/settings`）。
   - **已在线（`status === 'active'`），但 `last_inbound_at` 为空或距今超过 20 小时**：显示黄色提示「微信 iLink 协议要求用户主动发消息建立会话上下文，请在手机微信向您的机器人发送任意消息（如“在吗”），否则定时推送将在 30 分钟窗口内等待唤醒」。
   - **已在线且 20 小时内互动过**：显示绿色就绪态（含绑定机器人 ID 与最近互动时间），可直接点击「发送测试消息」。
3. 修正 `handleTest()` 保存触发条件：选中 `wechat_ilink` 时即便 `secret_key` 和 `webhook_url` 均为空，也先调用 `schedulerApi.saveChannel()` 持久化 `channel_type = 'wechat_ilink'` 再调用 `schedulerApi.testChannel()`，避免刚切换类型测试仍打到旧通道。

### 7.3 调度中心与日志展示（`Scheduler.vue` / `JobLogDrawer.vue`）

- `frontend/src/views/Scheduler.vue`：`formatChannelType` 新增 `case 'wechat_ilink': return '微信 iLink 机器人'`。
- `frontend/src/components/scheduler/JobLogDrawer.vue`：`getNotifyStatusType` / `getNotifyStatusLabel` 扩展：
  - `'queued'` → `type="warning"`，文案 `"待推送"`
  - `'partial'` → `type="warning"`，文案 `"部分送达"`
  - 既有 `'sent'`（`"已推微信"`）、`'failed'`（`"推送失败"`）、`'none'`（`"未推送"`）保持不变。

## 8. 错误处理矩阵

| 场景 | 行为与最终状态 |
|---|---|
| 用户未绑定 iLink 或状态非 `active` / `enabled=0` 触发定时任务 | 调度引擎直接写 `job_run_logs.notification_status = 'failed'` 与明确原因，不入队空等 30 分钟 |
| 网关离线 `< 30 分钟` 后恢复 | 消息留在 `im_push_outbox(status='pending')`，`ilink-runner` 重连后首个 `claim` 立即投递并改写 `job_run_logs` 为 `sent` |
| 网关离线 `>= 30 分钟` | `scheduler_engine_thread` 过期扫描将超时记录标为 `expired`，并将 `job_run_logs` 更新为 `failed`（附过期说明） |
| 有 `active` 实例但网关本地无 `context_token` | `kind='test'` 立即 `ack('waiting_context')` → `failed` 并提示先发消息；`kind='scheduler'` 保留在 `pending` 等 30 分钟内用户发消息唤醒 |
| 用户在 30 分钟内给机器人发消息 | `monitor.js` 写 `context.json` 并调用 `pusher.wake()`，立即 `claim` 补推窗口内的全部 `pending` 任务 |
| `sendText` 返回 `-2`（`context_token` 失效） | 本地置 `context.invalidated = true`；`scheduler` 回执 `waiting_context` 退回 `pending`（不耗重试次数）；`test` 立即失败并明确提示重新发消息激活 |
| `sendText` 返回 `-14`（`session_expired`） | 上报实例状态 `session_expired`，`scheduler` 回执 `retry` 保留在等待窗口，当前 `ilink-runner` 以退出码 `2` 退出等待重新扫码 |
| 发送过程中网断 / 超时 / HTTP 5xx | 单片重试 1 次；仍失败回执 `retry`，按 `10s/30s/60s/120s` 退避，最多 5 次后转 `failed` 并更新日志 |
| `ilink-runner` 在 `leased` 期间崩溃 | 60 秒租约到期后，下一轮 `claim`（或重启后的进程）自动回收租约回到 `pending` 继续投递 |
| 内置全局任务广播时部分通道成功、部分失败 | 同步+异步汇总完成后 `job_run_logs.notification_status = 'partial'`，保留失败摘要 |
| 网关跨实例伪造 `ack` (`push_id` 属其他 `instance_id`) | 后端强制按 `id AND instance_id` 更新，返回 `1003` 不生效 |
| 测试推送 10 秒内网关未消费 | 后端主动将 `kind='test'` 标记 `expired` 并向前端报错，防止网关恢复后补推迟到的测试文本 |

## 9. 测试与验证方案

### 9.1 网关测试（`node --test gateway/ilink/test/`）

- `state.test.js`：补充 `context.json` 原子读写、`0600` 权限、清理与不同实例隔离测试。
- `client.test.js`：补充 `sendText` 返回 `ret: -14`（`IlinkSessionExpiredError`）、`ret: -2`（`IlinkContextInvalidError`）与普通非 0 错误分类测试。
- `formatter.test.js`（新增）：Markdown 去噪（标题/加粗/引用/链接/空行折叠）、自动前置标题、按换行自然断行 1800 字分片。
- `pusher.test.js`（新增）：
  - 正常 `claim → sendText → ack('sent')` 与 500ms 节流间隔；
  - `has_context = false` 时跳过 `scheduler` 仅领 `test` 回执失败；
  - `monitor` 收到入站消息后 `context.json` 更新并触发 `wake()` 立刻补推等待中的 `scheduler`；
  - `sendText` 抛 `IlinkContextInvalidError (-2)` 标记失效且 `scheduler` 回执 `waiting_context`；
  - `IlinkSessionExpiredError (-14)` 上报 `session_expired` 并向 `runner` 抛出终止；
  - 单片失败原地重试成功不重发首片、连续失败回执 `retry`。

### 9.2 后端单元测试（CTest / SQLite）

新增/扩展单元测试覆盖：

- `im_push_outbox` CRUD：入队、`claim` 优先级（`test` 先于 `scheduler`）、`has_context=false` 过滤、60s 租约回收、`waiting_context` 不消耗 `attempts`、`retry` 退避上限 `max_attempts=5`、`expire_stale` 清理与 `job_run_logs` 汇总状态（`queued` / `sent` / `partial` / `failed`）。
- 跨实例访问隔离：实例 B 尝试 `ack` 实例 A 的 `push_id` 返回 `1003`；停用/未激活实例 `claim` 返回 `1004`。
- `scheduler_usecase_save_channel` / `get_channel` / `test_channel` 覆盖 `wechat_ilink`（含 `ilink_status` 拼装及无实例/未激活/超时失效分支）。
- 内置任务广播单元测试：模拟多用户配置混合通道（1 个 `serverchan` + 2 个 `wechat_ilink`）验证广播分发与最终状态聚合。

### 9.3 双引擎集成测试（SQLite + PostgreSQL）

新增集成脚本 `backend/tests/test_ilink_scheduler_push.sh`（引用 `db_helper.sh`）：

1. 配置用户 `wechat_ilink` 推送通道，未登录前点击测试拦截报错；
2. 模拟网关激活实例后创建自定义任务并手动/定时执行 → `job_run_logs.notification_status == 'queued'`；
3. 模拟网关带 `has_context=false` 调用 `claim` → `scheduler` 任务保留不被误消耗；
4. 模拟网关带 `has_context=true` 与 `context_updated_at` 调用 `claim` → 原子拿到 `leased` 任务并更新 `im_bot_instances.last_inbound_at`；
5. 调用 `ack('sent')` → 验证 `job_run_logs.notification_status == 'sent'`；
6. 构造多用户测试内置全局任务广播 → 验证所有活跃 `wechat_ilink` 用户均生成 Outbox 记录及全部回执后的终态汇总；
7. 测试超过 30 分钟过期扫描 → 验证 `im_push_outbox.status == 'expired'` 且日志转为 `failed`。

### 9.4 前端验证与全量构建

```bash
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh unit backend/build
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token bash backend/tests/test_im_bot_instance.sh
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token bash backend/tests/test_ilink_scheduler_push.sh
npm --prefix gateway test
npm --prefix frontend test
npm --prefix frontend run build
```

## 10. 验收标准

1. 用户在调度中心可将「个人微信推送通道」切换并保存为 **微信 iLink 机器人（`wechat_ilink`）**，无需填写任何密钥或 Webhook URL；面板准确显示实例在线状态与最近微信互动时间。
2. 点击「发送测试消息」：当网关在线且存在有效 `context_token` 时，10 秒内收到测试推送并提示成功；若缺少 `context_token` 或已失效，向用户返回明确引导提示（先在微信发一条消息给机器人）。
3. 个人定时任务或手动「立即执行」触发后，`job_run_logs` 初始为 `queued`（待推送），网关发出后自动变为 `sent`；文本格式经过清晰排版与合理断行分片。
4. 若触发时网关离线或缺少 `context_token`，任务保留在 30 分钟队列中；用户在 30 分钟内重连网关或发送微信消息刷新上下文后自动补发；超时 30 分钟未送达后自动标为 `expired` 并将日志置为 `failed`。
5. 内置全局任务（如「AI 每日金融快讯提炼」）定时触发时，所有启用了推送通道的用户（含 `wechat_ilink` 与既有外部通道）均能收到推送，且状态聚合准确。
6. `bot_token` 与 `context_token` 始终停留在网关本地 `0600` 文件中，后端 DB 与前端零敏感凭证暴露；现有 Server酱 / PushDeer / 企微 / Webhook 与 iLink 对话链路全部回归通过。
