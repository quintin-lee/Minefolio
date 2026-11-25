# iLink 微信用户级独立实例 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为每个 Minefolio 用户提供一个独立 iLink 微信机器人实例，凭证/状态/进程隔离，AI 查询仍走现有只读 Runtime。

**Architecture:** 后端新增 `im_bot_instances` 注册表与 owner-facing API；Node 网关 supervisor 对账注册表并为每个实例派生独立子进程；子进程实现 iLink 扫码登录与长轮询，`bot_token` 只落在每实例 state 目录；网关通过 `X-Gateway-Token` 调后端代为发起 `im_chat` 异步任务。

**Tech Stack:** C23/csilk/SQLite/PostgreSQL；Node 18+（内置 fetch/crypto/child_process，零依赖）；Vue 3 + TypeScript + Element Plus + `qrcode`。

**Spec:** `docs/superpowers/specs/2026-11-25-im-bot-ilink-instances-design.md`

> 本会话用户明确要求 **不使用 subagents**，所有步骤由主代理直接执行。

---

## Chunk 1: 后端持久化与会话修复

### Task 1: V035 迁移

**Files:**
- Create: `backend/sql/migrations/sqlite/V035__im_bot_instances.sql`
- Create: `backend/sql/migrations/postgres/V035__im_bot_instances.sql`

- [ ] **Step 1: 写 SQLite 迁移**（内容见 spec §4.1，含 3 个索引）
- [ ] **Step 2: 写 PostgreSQL 迁移**（内容见 spec §4.2，时间字段用 `TIMESTAMPTZ`）
- [ ] **Step 3: 验证迁移可执行**

Run:
```bash
cd backend
rm -f /tmp/mf-v035.db
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/mf-v035.db ./build/minefolio --migrate-only 2>/dev/null || true
sqlite3 /tmp/mf-v035.db '.schema im_bot_instances'
```
Expected: 表与 3 个索引存在。若 `--migrate-only` 不存在，启动服务器后立即 `kill`，再查 schema。

### Task 2: 实例仓储函数

**Files:**
- Modify: `backend/src/infrastructure/repositories/im_bot_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/im_bot_repo_impl.c`

新增（返回 `csilk_json_t*` 或 int，沿用现有风格）：

```c
csilk_json_t* mf_im_bot_instance_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* mf_im_bot_instance_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* mf_im_bot_instance_get_any(csilk_db_pool_t* pool, int64_t id);
csilk_json_t* mf_im_bot_instance_list_all(csilk_db_pool_t* pool);
int64_t mf_im_bot_instance_create(csilk_db_pool_t* pool, int64_t user_id,
                                  const char* platform, const char* adapter);
int mf_im_bot_instance_relogin(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int mf_im_bot_instance_set_enabled(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled);
int mf_im_bot_instance_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int mf_im_bot_instance_submit_verify_code(csilk_db_pool_t* pool, int64_t user_id,
                                          int64_t id, const char* code);
int mf_im_bot_instance_consume_verify_code(csilk_db_pool_t* pool, int64_t id,
                                           char* out, size_t out_cap);
int mf_im_bot_instance_update_status(csilk_db_pool_t* pool, int64_t id,
                                     const char* status, const char* qr_content,
                                     const char* qr_expires_at, const char* last_error);
int mf_im_bot_instance_activate(csilk_db_pool_t* pool, int64_t id,
                                const char* ilink_bot_id, const char* ilink_user_id,
                                char* err_buf, size_t err_cap);
int mf_im_bot_instance_heartbeat(csilk_db_pool_t* pool, int64_t id,
                                 char* status_out, size_t status_cap,
                                 int64_t* epoch_out, int* enabled_out);
```

- [ ] **Step 1: 实现查询函数**。字段用 `db_get_int/db_get_num/db_get_bool` 解析；`qr_content` 仅在非终态返回；绝不返回 `pending_verify_code`。
- [ ] **Step 2: 实现 `create`**。`INSERT ... RETURNING id`；唯一约束冲突返回 `-2`（已存在）。
- [ ] **Step 3: 实现 `relogin`**。`UPDATE ... SET login_epoch=login_epoch+1, status='pending_login', qr_content=NULL, qr_expires_at=NULL, pending_verify_code=NULL, pending_verify_expires_at=NULL, verify_attempts=0, last_error=NULL, updated_at=datetime('now') WHERE id=? AND mf_user_id=?`。
- [ ] **Step 4: 实现 `set_enabled`**。disable: `enabled=0,status='disabled',qr_content=NULL,...`；enable: `enabled=1,status='pending_login',login_epoch=login_epoch+1,...`。
- [ ] **Step 5: 实现 `delete`**。事务内先取 `im_user_id`，删除 `im_bot_bindings`（`platform='wechat' AND im_user_id=? AND mf_user_id=?`），再删实例（`WHERE id=? AND mf_user_id=?`）。
- [ ] **Step 6: 实现验证码**。`submit`: 仅 owner + 状态合法 + 数字 4–8 位 + `verify_attempts<10`，写 `pending_verify_code` 和 `datetime('now','+120 seconds')`；`consume`: `UPDATE ... RETURNING pending_verify_code` 同时清空，过期则返回 0。
- [ ] **Step 7: 实现 `activate`（关键事务）**：
  1. `SELECT mf_user_id, im_user_id FROM im_bot_instances WHERE id=?`；
  2. `im_user_id = 'ilink:' + ilink_user_id`；
  3. 查 `im_bot_instances` 是否有其他 id 占用同 `im_user_id` 或 `ilink_bot_id` → `-2`；
  4. 查 `im_bot_bindings` 是否被其他 `mf_user_id` 占用 → `-3`；
  5. upsert 绑定；
  6. 若旧 `im_user_id` 非空且不同，删除本 owner 的旧绑定；
  7. 更新实例为 `active`，清空二维码/验证码；
  8. 提交。
- [ ] **Step 8: 实现 `update_status` / `heartbeat`**。`heartbeat` 返回状态/epoch/enabled 并 touch `last_seen_at`。
- [ ] **Step 9: 编译检查**

Run: `./backend/tests/run_isolated.sh build backend/build`
Expected: 0 errors。

### Task 3: 会话按标题精确查找

**Files:**
- Modify: `backend/src/infrastructure/repositories/ai_session_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/ai_session_repo_impl.c`
- Modify: `backend/src/application/ai/im_bot_usecases.c`（`im_bot_usecase_chat_submit` L302-397）

- [ ] **Step 1: 新增声明/实现**

```c
int64_t mf_ai_session_find_by_title(csilk_db_pool_t* pool, int64_t user_id, const char* title);
```

SQL:
```sql
SELECT id FROM ai_sessions WHERE user_id=? AND title=? ORDER BY id DESC LIMIT 1
```

- [ ] **Step 2: 替换 50 条扫描逻辑**为：
```c
int64_t session_id = mf_ai_session_find_by_title(db_pool, user_id, title);
if (session_id <= 0) session_id = mf_ai_session_insert(db_pool, user_id, title, "im-bot", "system");
```

- [ ] **Step 3: 编译**：`./backend/tests/run_isolated.sh build backend/build`。

---

## Chunk 2: 后端用例与 API

### Task 4: 实例用例层

**Files:**
- Create: `backend/src/application/ai/im_bot_instance_usecases.h`
- Create: `backend/src/application/ai/im_bot_instance_usecases.c`

函数（全部 `void* pool` 风格）：

```c
int im_instance_usecase_create(void* pool, int64_t user_id, const char* platform,
                               const char* adapter, csilk_json_t** out, int* code,
                               char* err, size_t err_cap);
csilk_json_t* im_instance_usecase_list(void* pool, int64_t user_id);
csilk_json_t* im_instance_usecase_get(void* pool, int64_t user_id, int64_t id);
int im_instance_usecase_relogin(void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap);
int im_instance_usecase_verify_code(void* pool, int64_t user_id, int64_t id, const char* code, int* code_out, char* err, size_t err_cap);
int im_instance_usecase_set_enabled(void* pool, int64_t user_id, int64_t id, int enabled, int* code, char* err, size_t err_cap);
int im_instance_usecase_delete(void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap);

csilk_json_t* im_instance_gateway_list(void* pool);
csilk_json_t* im_instance_gateway_get(void* pool, int64_t id);          /* 消费验证码 */
int im_instance_gateway_status(void* pool, int64_t id, csilk_json_t* body, int* code, char* err, size_t err_cap);
int im_instance_gateway_heartbeat(void* pool, int64_t id, csilk_json_t** out, int* code, char* err, size_t err_cap);
int im_instance_gateway_chat(void* pool, int64_t id, const char* im_user_id, const char* message,
                             csilk_json_t** out_sync, int64_t* task_id, bool* dedup,
                             int64_t* session_id, int* code, char* err, size_t err_cap);
csilk_json_t* im_instance_gateway_task(void* pool, int64_t id, int64_t task_id, int* code);
```

- [ ] **Step 1: create** 校验 `MINEFOLIO_GATEWAY_TOKEN`（`config_secret_get("GATEWAY_TOKEN",...)`）非空；仅允许 `wechat`/`ilink`；调用 repo create；已存在 → 1004。
- [ ] **Step 2: get/list** 直接返回 repo JSON；get 非 owner 返回 NULL → controller 报 1003。
- [ ] **Step 3: relogin/verify/enable/disable/delete** 调 repo，映射错误码。
- [ ] **Step 4: gateway_status**：校验状态枚举（禁止 `disabled`）；`active` 调 repo activate；其余调 update_status；`qr_ready` 要求 `qr_content` 非空。
- [ ] **Step 5: gateway_chat**：
  - `mf_im_bot_instance_get_any`；不存在/disabled/非 active → 1003/1004；
  - 比较 `im_user_id` 与实例的 `im_user_id`，不等 → 1004；
  - 调 `im_bot_usecase_chat_submit(pool, instance.mf_user_id, "wechat", instance.im_user_id, message, ...)`；
  - 把同步未绑定结果原样返回。
- [ ] **Step 6: gateway_task**：取实例 → `mf_async_task_get(pool, instance.mf_user_id, task_id)` → 校验 `task_type='im_chat'`。
- [ ] **Step 7: 编译**。

### Task 5: 控制器、网关鉴权、中间件放行

**Files:**
- Create: `backend/src/middlewares/gateway_auth.h`
- Create: `backend/src/middlewares/gateway_auth.c`
- Create: `backend/src/interfaces/http/controllers/im_bot_instance_controller.h`
- Create: `backend/src/interfaces/http/controllers/im_bot_instance_controller.c`
- Modify: `backend/src/main.c`
- Modify: `backend/src/middlewares/jwt_middleware.c`
- Modify: `backend/src/middlewares/csrf_middleware.c`
- Modify: `backend/CMakeLists.txt`（若源文件显式列出）

- [ ] **Step 1: `gateway_auth_check(csilk_ctx_t* c)`**：`config_secret_get("GATEWAY_TOKEN",...)`；空 → `1001 "网关未启用"` 并返回 0；取 `X-Gateway-Token`，长度相等时逐字节 XOR 比较（常量时间），失败 → `1001`。
- [ ] **Step 2: owner 控制器路由**：
```c
POST   /api/im/instances
GET    /api/im/instances
GET    /api/im/instances/:id
POST   /api/im/instances/:id/relogin
POST   /api/im/instances/:id/verify-code
POST   /api/im/instances/:id/enable
POST   /api/im/instances/:id/disable
DELETE /api/im/instances/:id
```
- [ ] **Step 3: gateway 控制器路由**（每个 handler 第一行 `if (!gateway_auth_check(c)) return;`）：
```c
GET    /api/gateway/im/instances
GET    /api/gateway/im/instances/:id
PATCH  /api/gateway/im/instances/:id/status
POST   /api/gateway/im/instances/:id/heartbeat
POST   /api/gateway/im/instances/:id/chat
GET    /api/gateway/im/instances/:id/tasks/:task_id
```
- [ ] **Step 4: JWT 放行**：`jwt_middleware_wrapper` 开头判断 `strncmp(path, "/api/gateway/", 13) == 0` → `csilk_next` 并 return。
- [ ] **Step 5: CSRF 放行**：`is_csrf_excluded_path()` 增加 `/api/gateway/` 前缀。
- [ ] **Step 6: 注册**：`main.c` 末尾 `register_im_bot_instance_routes(app);`。
- [ ] **Step 7: CMake** 加入新 `.c` 文件（检查 `CMakeLists.txt` 的 `add_library(minefolio_core ...)` 列表）。
- [ ] **Step 8: 编译**。

### Task 6: 后端集成测试

**Files:**
- Create: `backend/tests/test_im_bot_instance.sh`（source `db_helper.sh`，模式参考 `test_im_bot.sh`）

覆盖 spec §10 的 12 条。至少包含：

- [ ] 创建 A 实例 → 200/code 0
- [ ] B 列表为空；B GET/DELETE A → 1003
- [ ] 网关无 token/错 token → 1001
- [ ] 上报 qr_ready → A 可见，B 不可见
- [ ] 上报 active → `im_bot_bindings` 出现 `wechat / ilink:<id>`，二维码清空
- [ ] 重复 ilink_user_id 第二个实例 → 1004
- [ ] gateway chat 正确 `im_user_id` → task_id；错误 → 1004
- [ ] 轮询 gateway task
- [ ] relogin → epoch+1，pending_login
- [ ] 新 ilink_user_id 确认 → 旧绑定删除
- [ ] disable → heartbeat 1004；enable → epoch+1
- [ ] DELETE → 实例与绑定都删除
- [ ] 验证码消费一次；第二次为空
- [ ] >50 会话仍复用同一 IM 会话
- [ ] 运行：`MINEFOLIO_GATEWAY_TOKEN=test-gateway-token ./backend/tests/test_im_bot_instance.sh`

---

## Chunk 3: 网关

### Task 7: iLink client + state

**Files:**
- Create: `gateway/ilink/client.js`
- Create: `gateway/ilink/state.js`
- Create: `gateway/ilink/test/client.test.js`
- Create: `gateway/ilink/test/state.test.js`

- [ ] **Step 1: client**：常量 `BASE_URL`、`APP_ID='bot'`、`CLIENT_VERSION='196608'`、`CHANNEL_VERSION='3.0.0-minefolio'`、`BOT_AGENT='Minefolio/0.1.0'`；`buildHeaders({token, commonOnly})`；`baseInfo()`；方法：
  - `getBotQrcode()`
  - `getQrcodeStatus({qrcode, verifyCode, baseUrl, timeoutMs})`
  - `getUpdates({account, cursor})`
  - `sendText({account, toUserId, contextToken, text})`
  - `IlinkSessionExpiredError`（ret/errcode -14）
  - 所有请求 `AbortSignal.timeout`；超时返回可识别错误。
- [ ] **Step 2: state**：`instanceDir(id)`、`readJson/writeJsonAtomic`（mkdir 0700，文件 0600，tmp+fsync+rename）、`readAccount/writeAccount/clearAccount`、`readCursor/writeCursor`、`readDedup/writeDedup`、`removeInstanceDir`。
- [ ] **Step 3: 测试**：headers 断言；-14 分类；两实例目录互不干扰；0600；损坏 JSON 返回 null。
- [ ] **Step 4: 运行**：`node --test gateway/ilink/test/`。

### Task 8: 登录状态机

**Files:**
- Create: `gateway/ilink/login.js`
- Create: `gateway/ilink/test/login.test.js`

- [ ] **Step 1: `runLogin({client, state, onStatus, fetchVerifyCode, log, signal})`**
  - `getBotQrcode` → `onStatus({status:'qr_ready', qr_content})`
  - 循环 `getQrcodeStatus`：`wait` 继续；`scaned` → `scanned`；`need_verifycode`（含 `confirmed_but_need_verifycode`）→ 上报并取 PIN；`scaned_but_redirect` 切 baseUrl；`confirmed` 保存账户并返回 `{ok:true, account}`；`expired`/`verify_code_blocked` 刷新≤3；`binded_redirect` 返回 `{ok:false, reason:'already_bound'}`。
  - 等待 PIN 时 5s 短轮询；其余 40s。
- [ ] **Step 2: 测试**：wait→scaned→need→scaned→confirmed；redirect；expired 三次后失败；binded_redirect；QR content 透传。

### Task 9: monitor + runner

**Files:**
- Create: `gateway/ilink/monitor.js`
- Create: `gateway/ilink/runner.js`
- Create: `gateway/ilink/test/monitor.test.js`

- [ ] **Step 1: monitor**：
  - `runMonitor({client, state, account, instanceId, api, log, signal})`
  - 循环 `getUpdates`；保存非空 cursor；过滤 `message_type===2`、`group_id`、非 owner、重复 message_id（LRU 2000 + 持久化 1000）。
  - 文本：type 1 text；type 3 voice transcription；其他每 10 分钟提示一次。
  - 串行队列上限 `ILINK_MAX_QUEUE`；满时每 60s 提示一次。
  - `api.chat(instanceId, imUserId, text)` + `api.pollTask`；回复 `sendText`，按 `ILINK_REPLY_CHUNK` 分片。
  - `-14` 抛 `IlinkSessionExpiredError`；网络错误退避 2s→60s。
- [ ] **Step 2: runner**：
  - 解析 `--instance`、`--force-login`；`GET /api/gateway/im/instances/:id`；不存在/disabled → exit 3/0。
  - `--force-login` 或 `account.json` 不存在 → `runLogin`；否则 `runMonitor`。
  - 心跳 30s；返回 disabled/1003/1004 → 退出。
  - 退出码：0 正常、2 需重登、3 配置/鉴权、4 绑定冲突。
- [ ] **Step 3: 测试**：mock iLink server + mock backend，覆盖文本、过滤、去重、context_token、队列上限、-14。

### Task 10: supervisor + server

**Files:**
- Create: `gateway/ilink/supervisor.js`
- Create: `gateway/ilink-server.js`
- Create: `gateway/ilink/test/supervisor.test.js`
- Modify: `gateway/package.json`（`"ilink": "node ilink-server.js"`，test 脚本包含 `ilink/test/`）

- [ ] **Step 1: supervisor**：`createSupervisor({api, spawn, kill, stateRoot, log, ...})`，导出 `reconcile(instances)` 便于注入测试：
  - 需要的状态集合；`login_epoch` 变化重启；删除/禁用杀进程并 `removeInstanceDir`；崩溃退避 2/4/8/16/30s；5 分钟 5 次 → 上报 error；`ILINK_MAX_INSTANCES`。
  - SIGTERM/SIGINT 优雅关闭。
- [ ] **Step 2: `ilink-server.js`**：校验 `MINEFOLIO_GATEWAY_TOKEN`；创建 API client（`X-Gateway-Token`）；启动 supervisor。
- [ ] **Step 3: 测试**：注入 fake spawn/kill，断言启动/停止/epoch 重启/删除清理/退避。

---

## Chunk 4: 前端

### Task 11: API 与类型

**Files:**
- Modify: `frontend/src/api/im.ts`
- Modify: `frontend/src/types/index.ts`

- [ ] **Step 1: 类型** `ImBotInstance`、`ImBotInstanceStatus`。
- [ ] **Step 2: API**：`listInstances/getInstance/createInstance/relogin/verifyCode/enable/disable/deleteInstance`，全部走 `http`。
- [ ] **Step 3: 测试**（如现有 API 测试模式）或类型检查。

### Task 12: ImBotSettings UI

**Files:**
- Modify: `frontend/src/components/settings/ImBotSettings.vue`

- [ ] **Step 1: 新增 iLink 区块**：创建按钮、状态标签、二维码（`qrcode` 包）、PIN 输入、重新登录/停用/删除。
- [ ] **Step 2: 轮询**：`pending_login|qr_ready|scanned|need_verifycode` 每 2s；`onUnmounted` 清理；`try/catch`。
- [ ] **Step 3: 错误展示**：`iLink 网关未配置`、`网关离线，凭证将在网关恢复后清除`。
- [ ] **Step 4: `npm --prefix frontend run build`** 0 错误；`npm --prefix frontend test` 通过。

---

## Chunk 5: 部署、文档与全量验证

### Task 13: compose / gitignore / docs

**Files:**
- Modify: `docker-compose.yml`
- Modify: `docker-compose.im.yml`
- Modify: `.gitignore`
- Create: `docs/design/im-bot-ilink-instances.md`
- Modify: `gateway/README.md`
- Modify: `docs/design/im-bot-design.md`

- [ ] **Step 1: compose** 增加 `ilink-gateway`（spec §11），后端注入 `MINEFOLIO_GATEWAY_TOKEN`，声明 `ilink_state` volume。
- [ ] **Step 2: `.gitignore`** 加 `gateway/.ilink-state/`。
- [ ] **Step 3: 文档**：架构、状态机、部署、安全不变量、风险。
- [ ] **Step 4: 语法检查** `docker compose config`（若可用）。

### Task 14: 全量验证

- [ ] **Step 1:** `./backend/tests/run_isolated.sh build backend/build`
- [ ] **Step 2:** `./backend/tests/run_isolated.sh unit backend/build`
- [ ] **Step 3:** `MINEFOLIO_GATEWAY_TOKEN=test-gateway-token ./backend/tests/test_im_bot_instance.sh`
- [ ] **Step 4:** `MINEFOLIO_GATEWAY_TOKEN=test-gateway-token ./backend/tests/test_im_bot.sh`
- [ ] **Step 5:** `npm --prefix gateway test`
- [ ] **Step 6:** `npm --prefix frontend test`
- [ ] **Step 7:** `npm --prefix frontend run build`
- [ ] **Step 8:** 手工烟测：启动后端 + 网关（mock iLink 或真实扫码），验证创建实例 → 扫码/状态 → active → 私聊得到回复 → 删除清理。

---

## 关键风险与决策记录

- **不存 `bot_token` 到后端**：后端只能通过注册表控制生命周期；凭证撤销依赖 supervisor 删除 state。
- **网关 service token 是单点**：部署在内网，文档要求强随机、可轮换；所有 gateway handler 常量时间比较，未配置即拒绝。
- **一个用户一个实例**：`UNIQUE(mf_user_id, platform, adapter)`；换号走 relogin/delete。
- **一个机器人号/owner 号只能绑一个 Minefolio 用户**：部分唯一索引 + activate 事务。
- **非 owner 静默丢弃**：不回复、不查库，避免探测与放大。
- **只读工具白名单不变**：网关 chat 复用 `im_bot_usecase_chat_submit` → `im_chat_task_fn`。
