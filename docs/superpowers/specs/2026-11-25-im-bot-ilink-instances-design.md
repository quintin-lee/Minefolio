# iLink 微信接入 — 用户级独立实例设计

**Date:** 2026-11-25
**Status:** Approved (design), pending spec review
**Scope:** Minefolio backend + gateway + frontend

## 1. Goal

让每个 Minefolio 用户拥有一个独立的 iLink 微信机器人实例：

- 独立微信机器人账号（用户自行准备）
- 独立 `bot_token`，只保存在网关本地 state 目录（0600）
- 独立 Node 子进程
- 只有该实例 owner 的微信 `ilink_user_id` 能触发查询
- 所有查询走现有 AI Runtime 的只读工具白名单

非目标（本迭代不做）：

- 一个共享机器人服务多个 Minefolio 用户（现有 `/bind` 路径仍保留，但不作为 iLink 主路径）
- 群聊、图片、文件、视频、媒体上传、typing 指示
- other-owner 实名认证流程
- 将 `bot_token` 存入后端数据库
- 自动迁移已有 wechaty 用户

## 2. 核心不变量

1. 所有 owner-facing 查询必须 `WHERE mf_user_id = ctx_user_id`。
2. 网关接口只接受 `X-Gateway-Token`，常量时间比较；未配置 token 时接口拒绝服务。
3. 网关 chat 请求由后端从 `im_bot_instances.mf_user_id` 解析 owner，并强制校验 `im_user_id == instance.im_user_id`。
4. 一个微信机器人号（`ilink_bot_id`）和一个 owner 微信号（`im_user_id`）各自只能绑定一个 Minefolio 用户；冲突返回 `1004`。
5. `bot_token` 永不离开网关；不写日志、不进后端、不进前端。
6. 二维码内容只返回给 owner，且只在登录状态短期保存；`active`/`expired`/删除时清空。
7. 手机验证码一次性消费、限时、限次、仅 owner 可提交。
8. 非 owner 微信消息直接丢弃，不触发任何数据查询，也不回复（避免放大）。
9. AI 工具仍只挂载 `ai_tool_get_readonly_csilk_definitions()`。
10. 删除实例后，网关 supervisor 必须杀掉子进程并删除该实例 state 目录（含 `bot_token`）。

## 3. 总体架构

```text
微信 owner
   │ 私聊
   ▼
微信机器人号
   │ iLink getupdates / sendmessage
   ▼
ilink-runner 子进程（每实例一个，独立 state 目录）
   │ X-Gateway-Token
   ▼
Minefolio 后端 /api/gateway/im/*
   │ instance -> mf_user_id
   ▼
现有 AI Runtime（只读工具白名单）
   │ mf_async_task_submit("im_chat")
   ▼
ilink-runner 轮询任务 -> sendmessage -> 微信

App（Vue）
   │ JWT + CSRF
   ▼
后端 /api/im/instances/*  （二维码/状态/验证码）
```

iLink 协议只在 Node 网关实现。后端不实现 iLink 登录状态机，也不保存 bot 凭证。

## 4. 数据模型

新增迁移 `V035__im_bot_instances.sql`（sqlite + postgres）。

### 4.1 SQLite

```sql
CREATE TABLE IF NOT EXISTS im_bot_instances (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  mf_user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  platform TEXT NOT NULL DEFAULT 'wechat',
  adapter TEXT NOT NULL DEFAULT 'ilink',
  ilink_bot_id TEXT,
  ilink_user_id TEXT,
  im_user_id TEXT,
  status TEXT NOT NULL DEFAULT 'pending_login',
  qr_content TEXT,
  qr_expires_at TEXT,
  login_epoch INTEGER NOT NULL DEFAULT 0,
  pending_verify_code TEXT,
  pending_verify_expires_at TEXT,
  verify_attempts INTEGER NOT NULL DEFAULT 0,
  enabled INTEGER NOT NULL DEFAULT 1,
  last_error TEXT,
  last_seen_at TEXT,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now')),
  UNIQUE(mf_user_id, platform, adapter)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_im_user
  ON im_bot_instances(platform, im_user_id) WHERE im_user_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_bot
  ON im_bot_instances(platform, ilink_bot_id) WHERE ilink_bot_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_im_instances_user ON im_bot_instances(mf_user_id);
```

### 4.2 PostgreSQL

```sql
CREATE TABLE IF NOT EXISTS im_bot_instances (
  id BIGSERIAL PRIMARY KEY,
  mf_user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  platform TEXT NOT NULL DEFAULT 'wechat',
  adapter TEXT NOT NULL DEFAULT 'ilink',
  ilink_bot_id TEXT,
  ilink_user_id TEXT,
  im_user_id TEXT,
  status TEXT NOT NULL DEFAULT 'pending_login',
  qr_content TEXT,
  qr_expires_at TIMESTAMPTZ,
  login_epoch INTEGER NOT NULL DEFAULT 0,
  pending_verify_code TEXT,
  pending_verify_expires_at TIMESTAMPTZ,
  verify_attempts INTEGER NOT NULL DEFAULT 0,
  enabled INTEGER NOT NULL DEFAULT 1,
  last_error TEXT,
  last_seen_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  UNIQUE(mf_user_id, platform, adapter)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_im_user
  ON im_bot_instances(platform, im_user_id) WHERE im_user_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_bot
  ON im_bot_instances(platform, ilink_bot_id) WHERE ilink_bot_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_im_instances_user ON im_bot_instances(mf_user_id);
```

### 4.3 状态枚举

| 状态 | 含义 | 网关进程 |
|---|---|---|
| `pending_login` | 已创建/重新登录，等待 supervisor 启动 | 需要 |
| `qr_ready` | 二维码已生成 | 需要 |
| `scanned` | 已扫码，等待确认 | 需要 |
| `need_verifycode` | 需要手机 PIN | 需要 |
| `active` | 已登录，长轮询中 | 需要 |
| `session_expired` | iLink `-14`，等待用户重新登录 | 不启动 |
| `expired` | 二维码刷新 3 次仍过期 | 不启动 |
| `error` | 登录/绑定/运行错误 | 不自动重启 |
| `disabled` | 用户停用 | 不启动 |

允许的状态上报由网关 `PATCH .../status` 完成；`disabled` 只能由 owner 接口设置。

## 5. 后端 API

响应统一 `{code, message, data}`。

### 5.1 App（JWT + CSRF）

#### `POST /api/im/instances`

请求：`{"platform":"wechat","adapter":"ilink"}`（可省略，默认值）。

行为：

- 校验 `MINEFOLIO_GATEWAY_TOKEN` 已配置，否则 `1004 "iLink 网关未配置"`。
- 每用户只允许一条 `(wechat, ilink)`；已存在返回 `1004`。
- 插入 `status='pending_login'`，返回实例 JSON。

#### `GET /api/im/instances`

返回当前用户实例列表。字段：

```json
{
  "id": 1,
  "platform": "wechat",
  "adapter": "ilink",
  "status": "qr_ready",
  "qr_content": "https://...",
  "ilink_bot_id": null,
  "ilink_user_id": null,
  "im_user_id": null,
  "last_error": null,
  "enabled": true,
  "login_epoch": 0,
  "last_seen_at": "2026-11-25T12:00:00Z",
  "created_at": "...",
  "updated_at": "..."
}
```

`qr_content` 仅在 `pending_login`/`qr_ready`/`scanned`/`need_verifycode` 返回；否则为 `null`。绝不返回 `pending_verify_code`。

#### `GET /api/im/instances/:id`

同上，仅 owner 可读；非 owner 返回 `1003`（不泄露存在性）。

#### `POST /api/im/instances/:id/relogin`

- owner only。
- `login_epoch = login_epoch + 1`。
- `status='pending_login'`，清空 `qr_content`/`qr_expires_at`/`pending_verify_code`/`last_error`。
- 保留旧 `ilink_bot_id`/`ilink_user_id`/`im_user_id`，以便新确认时清理旧绑定。
- supervisor 检测 epoch 变化后杀掉子进程并以 `--force-login` 重启。

#### `POST /api/im/instances/:id/verify-code`

请求：`{"code":"123456"}`。

- owner only。
- 状态必须是 `need_verifycode`、`qr_ready` 或 `scanned`。
- 仅允许 4–8 位数字。
- 保存 `pending_verify_code` + `pending_verify_expires_at = now + 120s`，`verify_attempts += 1`。
- 同一登录流程最多 10 次；超过返回 `1004`，要求重新登录。
- 成功返回 `{ok:true}`。

#### `POST /api/im/instances/:id/enable` / `disable`

- owner only。
- `disable`：`enabled=0`，`status='disabled'`，清空二维码/验证码。
- `enable`：`enabled=1`，`status='pending_login'`，`login_epoch += 1`（确保网关重启并重新确认凭证）。

#### `DELETE /api/im/instances/:id`

- owner only。
- 同一事务内删除 `im_bot_instances` 行，并删除 `im_bot_bindings` 中 `(platform='wechat', im_user_id=instance.im_user_id, mf_user_id=owner)` 的绑定。
- 网关 supervisor 在下一次对账时杀掉子进程并删除 state 目录。
- 若网关离线，App 显示“网关离线，凭证将在网关恢复后清除”。

### 5.2 网关（`X-Gateway-Token`）

所有接口先校验 token；失败返回 `1001`。若后端未配置 token，返回 `1001 "网关未启用"`。

#### `GET /api/gateway/im/instances`

返回全部实例（含 disabled），用于 supervisor 对账：

```json
{
  "instances": [
    {"id":1,"mf_user_id":7,"platform":"wechat","adapter":"ilink","status":"active","login_epoch":0,"enabled":1}
  ]
}
```

#### `GET /api/gateway/im/instances/:id`

返回实例详情 + `pending_verify_code`（若未过期）。**该接口会一次性消费验证码**：读取后清空 `pending_verify_code`。

#### `PATCH /api/gateway/im/instances/:id/status`

请求字段：

```json
{
  "status": "qr_ready",
  "qr_content": "...",
  "qr_expires_at": "2026-11-25T12:02:00Z",
  "ilink_bot_id": null,
  "ilink_user_id": null,
  "last_error": null
}
```

行为：

- 仅允许状态枚举中的值；`disabled` 不允许网关设置。
- `qr_ready`：写 `qr_content`/`qr_expires_at`，清空旧 `last_error`。
- `scanned` / `need_verifycode`：更新状态；`need_verifycode` 时保留等待中的验证码（如果有）。
- `active`：**事务内**执行：
  1. 要求 `ilink_bot_id`、`ilink_user_id` 非空；
  2. 校验 `im_user_id = 'ilink:' + ilink_user_id` 未被其他实例占用（`1004`）；
  3. 校验 `ilink_bot_id` 未被其他实例占用（`1004`）；
  4. 校验 `im_bot_bindings(wechat, im_user_id)` 未被其他用户占用（`1004`）；
  5. upsert 绑定到 `instance.mf_user_id`，`scope='readonly'`；
  6. 若实例旧的 `im_user_id` 与新值不同，删除旧绑定（仅限本 owner）；
  7. 写 `ilink_bot_id`/`ilink_user_id`/`im_user_id`/`status='active'`/`last_error=NULL`，清空二维码与验证码；
  8. 提交。
- `session_expired` / `expired` / `error`：更新状态与 `last_error`，清空二维码/验证码。
- 更新 `last_seen_at = now`。

#### `POST /api/gateway/im/instances/:id/heartbeat`

请求：`{"status":"active"}`（可选）。

返回：`{"status":"active","login_epoch":0,"enabled":1}`。

- 若实例不存在/已删除 → `1003`；disabled → `1004`；child 收到后退出。
- 更新 `last_seen_at`。

#### `POST /api/gateway/im/instances/:id/chat`

请求：`{"im_user_id":"ilink:wxid_xxx","message":"本月支出多少？"}`。

行为：

- 加载实例；要求 `enabled=1`、`status='active'`。
- 校验 `im_user_id === instance.im_user_id`，否则 `1004`。
- 调用现有 `im_bot_usecase_chat_submit(pool, instance.mf_user_id, "wechat", instance.im_user_id, message, ...)`。
- 该用例内部仍会校验 `im_bot_bindings` 的 `bound == mf_user_id`；绑定失效返回未绑定回复/`1004`。
- 返回与现有 chat 一致的 `{task_id, deduplicated}`。

#### `GET /api/gateway/im/instances/:id/tasks/:task_id`

- 加载实例，调用 `mf_async_task_get(pool, instance.mf_user_id, task_id)`，要求 `task_type='im_chat'`。
- 返回现有任务 JSON（`status`、`result`、`error` 等）。

## 6. 网关设计

### 6.1 文件结构

```text
gateway/ilink/client.js        iLink HTTP 客户端（headers/QR/getupdates/sendmessage/错误分类）
gateway/ilink/state.js         每实例 state 目录，原子写，0600
gateway/ilink/login.js         QR 状态机、PIN、redirect、刷新上限 3
gateway/ilink/monitor.js       getupdates 循环、去重、owner 校验、串行队列
gateway/ilink/runner.js        单实例子进程入口
gateway/ilink/supervisor.js    对账后端注册表，spawn/kill/重启/清理孤儿 state
gateway/ilink-server.js        可运行入口
```

零外部依赖，Node 内置 `fetch`/`crypto`/`fs`/`child_process`。

### 6.2 配置

| 变量 | 默认 | 说明 |
|---|---|---|
| `MINEFOLIO_API` | `http://localhost:8080` | 后端地址 |
| `MINEFOLIO_GATEWAY_TOKEN` | 必填 | 网关服务 token |
| `ILINK_STATE_DIR` | `gateway/.ilink-state` | 实例 state 根目录 |
| `ILINK_SUPERVISOR_POLL_MS` | `5000` | 对账周期 |
| `ILINK_HTTP_TIMEOUT_MS` | `45000` | 长轮询超时（iLink 35s） |
| `ILINK_MAX_QUEUE` | `8` | 每实例待处理消息上限 |
| `ILINK_REPLY_CHUNK` | `1800` | 回复分片长度 |
| `ILINK_MAX_INSTANCES` | `20` | supervisor 最大子进程数 |
| `ILINK_LOG_LEVEL` | `info` | 日志级别 |

`MINEFOLIO_GATEWAY_TOKEN` 未设置时 `ilink-server.js` 立即退出并打印明确错误。

### 6.3 state 目录

```text
<ILINK_STATE_DIR>/<instance_id>/account.json  {bot_token, ilink_bot_id, baseurl, ilink_user_id}
<ILINK_STATE_DIR>/<instance_id>/cursor.json   {get_updates_buf}
<ILINK_STATE_DIR>/<instance_id>/dedup.json    {ids:[...]}
```

- 目录 0700，文件 0600。
- 原子写：临时文件 + fsync + rename。
- 不同实例目录完全独立，禁止共享内存/模块级 token 缓存。
- 删除实例后由 supervisor 删除整个目录。

### 6.4 登录状态机

```text
pending_login
   │ get_bot_qrcode
   ▼
qr_ready ──scaned──► scanned ──confirmed──► active
   │                    │
   │                    └─need_verifycode──► need_verifycode
   │                                              │ PIN
   │                                              ▼
   │                                          scanned
   │
   ├─expired/verify_code_blocked（刷新≤3）──► qr_ready
   └─刷新>3──► expired
```

- 每次 `get_qrcode_status` 长轮询 35s；超时/网络错误视为 `wait`。
- `scaned_but_redirect` 且响应含 `redirect_host` → 后续请求用 `https://<redirect_host>`。
- `binded_redirect` → 上报 `error "该机器人已绑定其他实例，请先在原实例解绑"` 并退出。
- `confirmed` 必须有 `ilink_bot_id`；保存 `bot_token`/`ilink_bot_id`/`baseurl`/`ilink_user_id`，上报 `active`。
- PIN：收到 `need_verifycode` 后上报状态；每次 poll 前从 `GET /api/gateway/im/instances/:id` 领取一次性 `pending_verify_code` 并带上。`scaned` 后清空本地 PIN。
- 等待 PIN 时使用 5s 短轮询（abort 视为 wait），以便及时响应 App 输入；其余情况 40s 超时。
- `expired`/`verify_code_blocked` 重新 `get_bot_qrcode`，刷新计数最多 3 次。

### 6.5 运行循环

- `POST /ilink/bot/getupdates`，持久化非空 `get_updates_buf`。
- 过滤：
  - `message_type === 2`（机器人自己）
  - `group_id` 非空（群聊）
  - `from_user_id !== account.ilink_user_id`（非 owner）
  - `message_id` 去重（内存 LRU 2000 + 持久化最近 1000）
- 提取文本：
  - `type=1`：`text_item.text`
  - `type=3`：`voice_item.text`（转写）若有
  - 其他类型：每 10 分钟最多回复一次“目前仅支持文字消息”，否则忽略
- 每实例串行队列（上限 8）。满队列时丢弃新消息并每 60s 最多回复一次“消息过多，请稍后再试”。
- 每条消息调用 `POST /api/gateway/im/instances/:id/chat`，然后轮询任务（150ms 间隔 / 60s 超时）。
- 回复 `POST /ilink/bot/sendmessage`，回传入站 `context_token`；长回复按 `ILINK_REPLY_CHUNK` 分片，每片独立 `client_id`。
- `ret`/`errcode == -14` → 上报 `session_expired`，停止轮询并退出。
- 网络错误指数退避 2s → 60s。
- 心跳每 30s；返回 `disabled`/`1003`/`1004` 时退出。

### 6.6 supervisor 对账

- 每 5s `GET /api/gateway/im/instances`。
- 需要子进程的状态：`pending_login`、`qr_ready`、`scanned`、`need_verifycode`、`active`。
- 不需要：`session_expired`、`expired`、`error`、`disabled`。
- `login_epoch` 变化 → SIGTERM 旧进程，带 `--force-login` 重启。
- 子进程异常退出 → 指数退避重启（2/4/8/16/30s）；5 分钟内 5 次失败 → 上报 `error` 并停止自动重启。
- 实例从注册表消失 → 杀进程并删除 state 目录。
- 超过 `ILINK_MAX_INSTANCES` → 多余实例不启动，日志告警（不自动改后端状态）。
- SIGTERM/SIGINT：停止对账，SIGTERM 所有子进程，10s 后 SIGKILL，退出。

### 6.7 runner 启动

- 参数：`--instance <id>`、`--force-login`。
- `GET /api/gateway/im/instances/:id` 获取详情；不存在/disabled → 退出。
- `--force-login` 或 `account.json` 不存在 → 登录模式；否则直接运行。
- 退出码：
  - `0` 正常停止
  - `2` 需要重新登录（session_expired）
  - `3` 配置/鉴权失败
  - `4` 绑定冲突/已绑定其他实例

## 7. 后端实现结构

| 层 | 文件 | 职责 |
|---|---|---|
| 迁移 | `backend/sql/migrations/{sqlite,postgres}/V035__im_bot_instances.sql` | 新表 |
| 领域契约 | `backend/src/domain/ai/im_bot.h`（扩展现有契约） | 实例结构/枚举/仓库函数声明 |
| 仓储 | `backend/src/infrastructure/repositories/im_bot_repo_impl.c` | 实例 CRUD、状态更新、绑定事务、验证码消费 |
| 用例 | `backend/src/application/ai/im_bot_instance_usecases.c/.h` | 创建/查询/relogin/verify/启停/删除/网关状态/心跳/chat 解析 |
| 控制器 | `backend/src/interfaces/http/controllers/im_bot_instance_controller.c/.h` | owner 路由 + gateway 路由 |
| 服务鉴权 | `backend/src/middlewares/gateway_auth.h/.c` | `X-Gateway-Token` 常量时间校验 |
| 会话查询 | `backend/src/infrastructure/repositories/ai_session_repo_impl.c` | 新增 `mf_ai_session_find_by_title(user_id,title)` |
| 注册 | `backend/src/main.c` | `register_im_bot_instance_routes(app)`；JWT/CSRF 跳过 `/api/gateway/*` |

### 7.1 会话查找修复

`im_bot_usecase_chat_submit` 当前用 `mf_ai_session_list(...,1,50,&total)` 扫前 50 条。改为：

```c
int64_t session_id = mf_ai_session_find_by_title(db_pool, user_id, title);
if (session_id <= 0) session_id = mf_ai_session_insert(db_pool, user_id, title, "im-bot", "system");
```

新增 SQL：

```sql
SELECT id FROM ai_sessions
WHERE user_id = ? AND title = ?
ORDER BY id DESC LIMIT 1
```

## 8. 前端设计

`frontend/src/components/settings/ImBotSettings.vue` 增加「微信机器人（iLink）」区块：

- 无实例：说明 + 「创建微信机器人」按钮。
- `pending_login`/`qr_ready`/`scanned`：二维码 + 状态文案，每 2s 轮询 `GET /api/im/instances/:id`。
- `need_verifycode`：手机 PIN 输入框 + 提交按钮。
- `active`：显示 `ilink_bot_id` / `ilink_user_id` + 「重新登录」「停用」「删除」。
- `expired`/`session_expired`/`error`：显示 `last_error` + 「重新登录」。
- 创建前提示：必须使用独立微信机器人号；每个 Minefolio 用户一个实例。
- 网关未配置：显示后端返回的 `iLink 网关未配置`，并给出部署提示。

二维码渲染：

```ts
import QRCode from 'qrcode'

if (/^(https?:|data:image\/)/.test(content)) return content
return await QRCode.toDataURL(content, { width: 260, margin: 1 })
```

API 封装加到 `frontend/src/api/im.ts`，类型加到 `frontend/src/types/index.ts`。组件卸载时清理轮询定时器；所有异步初始化 `try/catch`。

## 9. 错误处理矩阵

| 场景 | 行为 |
|---|---|
| 后端不可达 | supervisor/runner 指数退避；App 状态显示旧值；不崩溃 |
| 网关不可达 | App 显示“网关离线”；AI 无回复；后端数据不变 |
| 子进程崩溃 | supervisor 退避重启；5 次失败上报 `error` |
| iLink `-14` | 上报 `session_expired`，停止轮询，等待 App 重新登录 |
| 二维码过期 | 刷新最多 3 次；仍失败上报 `expired` |
| PIN 错误 | 下一轮仍返回 `need_verifycode`；App 再次提示 |
| PIN 被锁 | `verify_code_blocked` → 重新生成二维码 |
| 绑定冲突 | `active` 上报返回 1004；child 改报 `error` 并退出 |
| 消息重复 | LRU 去重，不重复查询 |
| 非 owner 消息 | 丢弃，不回复，不查数据 |
| 群聊/自己/媒体 | 过滤；媒体每 10 分钟提示一次 |
| 长回复 | 1800 字分片发送 |
| 发送失败 | 重试一次；`-14` 转为 `session_expired` |
| 实例删除 | supervisor 杀进程、删 state；后端解绑 |
| 网关 token 错误 | 后端 1001；网关日志脱敏并退避 |

## 10. 测试计划

### 网关（`node --test gateway/ilink/test/`）

- `client.test.js`：headers、base_info、QR 请求、getupdates、sendmessage、-14 分类、超时。
- `login.test.js`：wait → scaned → need_verifycode → scaned → confirmed；redirect；expired 刷新 3 次；binded_redirect；QR content 透传。
- `monitor.test.js`：文本/语音转写；自己/群聊/陌生人过滤；去重；cursor 持久化；context_token 回传；队列上限。
- `state.test.js`：两实例目录隔离、0600、原子写、损坏 JSON 恢复。
- `supervisor.test.js`：注入 spawn/kill/list；启动/停止/epoch 重启/删除清理/崩溃退避。

### 后端（`backend/tests/test_im_bot_instance.sh`）

1. 用户 A 创建实例；用户 B 列表为空、GET A 实例 1003、DELETE A 实例 1003。
2. 无 token / 错 token 访问 `/api/gateway/im/*` → 1001。
3. 网关上报 `qr_ready`（含二维码）→ A 能看到；B 看不到。
4. 网关上报 `active`（`ilink_bot_id`/`ilink_user_id`）→ 绑定创建，实例 active，二维码清空。
5. 同一 `ilink_user_id` 被另一实例确认 → 1004。
6. 网关 chat：正确 `im_user_id` 返回 task_id；错误 `im_user_id` 1004；任务轮询走网关端点。
7. `relogin` → `login_epoch+1`，状态 pending_login，二维码清空；旧绑定保留到新确认。
8. 新确认使用不同 `ilink_user_id` → 旧绑定删除，新绑定创建。
9. `disable` → 状态 disabled；网关 heartbeat 1004；`enable` → epoch+1。
10. `DELETE` → 实例删除、绑定删除；网关列表不再包含。
11. 验证码：提交后网关 GET 消费一次；第二次为空；过期后为空。
12. 会话查找：构造 >50 个会话后，IM 会话仍复用同一条（不再新建）。

### 前端

- `npm --prefix frontend test`：API 封装、状态映射、二维码渲染分支。
- `npm --prefix frontend run build`：0 错误。

### 全量验证

```bash
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh unit backend/build
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token ./backend/tests/test_im_bot_instance.sh
MINEFOLIO_GATEWAY_TOKEN=test-gateway-token ./backend/tests/test_im_bot.sh
npm --prefix gateway test
npm --prefix frontend test
npm --prefix frontend run build
```

## 11. 部署

### docker-compose

在 `im` profile 增加：

```yaml
  ilink-gateway:
    image: node:22-alpine
    working_dir: /app
    command: node ilink-server.js
    environment:
      MINEFOLIO_API: http://minefolio:8080
      MINEFOLIO_GATEWAY_TOKEN: ${MINEFOLIO_GATEWAY_TOKEN:?set MINEFOLIO_GATEWAY_TOKEN}
      ILINK_STATE_DIR: /state
    volumes:
      - ./gateway:/app:ro
      - ilink_state:/state
    depends_on:
      - minefolio
    restart: unless-stopped
    profiles: ["im"]
volumes:
  ilink_state:
```

后端服务同时注入 `MINEFOLIO_GATEWAY_TOKEN`。

### 本地开发

```bash
export MINEFOLIO_GATEWAY_TOKEN=dev-gateway-token
./scripts/dev.sh
MINEFOLIO_GATEWAY_TOKEN=dev-gateway-token node gateway/ilink-server.js
```

### 文件清理

- `.gitignore` 增加 `gateway/.ilink-state/`。
- state volume 含 `bot_token`，备份/权限按密钥处理。

## 12. 文档

- 新增 `docs/design/im-bot-ilink-instances.md`：架构、协议映射、状态机、部署、安全、风险。
- 更新 `gateway/README.md`：iLink 启动方式、环境变量、state 目录、安全不变量。
- 更新 `docs/design/im-bot-design.md`：注明用户级独立实例为 iLink 主路径。
- 更新 `docker-compose.im.yml`。

## 13. 风险与缓解

| 风险 | 缓解 |
|---|---|
| iLink 协议变更 | 协议集中在 `gateway/ilink/client.js`；mock 测试锁定行为 |
| 个人微信风控 | 强制使用独立机器人号、低频、只读；文档明确风险 |
| 网关 service token 泄漏 | 强随机 token、内网部署、常量时间比较、可轮换 |
| bot_token 落盘 | 每实例 0600、目录 0700、删除实例即清理、volume 权限 |
| 每用户一进程资源 | `ILINK_MAX_INSTANCES` 限制、串行队列、长轮询 |
| 绑定抢占 | 数据库唯一索引 + 原子 upsert + 1004 |
| 非 owner 探测 | 静默丢弃，不回复、不查库 |
| 后端重启丢失验证码 | 验证码存 DB，120s 过期，一次性消费 |
| 网关离线时删除实例 | supervisor 恢复后清理；App 明确提示 |

## 14. 验收标准

1. 用户 A 在 App 创建实例，扫码后二维码消失、状态 `active`，`im_bot_bindings` 出现 `wechat / ilink:<id>` 映射到 A。
2. A 的微信私聊机器人得到 AI 回复；任务 trace 的 `user_id` 为 A。
3. 用户 B 无法查看/删除 A 的实例；网关用 A 的实例 ID + B 的 `im_user_id` 查询返回 1004。
4. 两个实例的子进程/state/token 完全独立；A 的消息不会进入 B 的会话。
5. 非 owner 微信消息被丢弃，后端无查询记录。
6. 删除实例后行、绑定、网关 state 均被清理。
7. 重新登录会生成新二维码；新确认后旧绑定被清理。
8. `-14` 后状态 `session_expired`，不自动循环，App 可重新登录。
9. 现有 QQ/wechaty/`/bind` 路径测试仍通过。
10. 后端、网关、前端构建与测试全部通过。
