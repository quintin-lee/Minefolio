# 定时任务管理中心（Scheduler Center）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建通用的定时任务管理中心（Scheduler Center），提供独立的 `/scheduler` 页面，支持“AI 智能分析（Prompt 驱动）”与“结构化报表”双任务模式，支持时钟周期与市场事件双触发，并将提炼后的精炼简报通过通用推送协议直达用户个人微信。

**Architecture:** 
1. **基础设施与存储层**：Flyway 迁移 `V021` 创建 `scheduled_jobs`、`user_notification_channels` 与 `job_run_logs` 表，提供双引擎（SQLite 与 PostgreSQL）兼容的 DDD 仓储实现。
2. **核心引擎层**：在 C 后端构建 `core/scheduler/` 调度器，包含纯 C 5 段式 Cron 表达式解析器、毫秒级定时事件堆、带防刷冷却的行情事件总线（`scheduler_events`），以及最大 3 线程并发的工作线程池（`scheduler_worker_pool`）。
3. **服务与应用层**：封装 `wechat_push_service`（适配 Server酱、PushDeer 与通用 Webhook），打通 Unified AI Runtime 与报表用例层，提供严格多租户隔离与管理员系统级任务保护的 REST API。
4. **前端展现层**：在 Vue 3 SPA 中增加 `/scheduler` 一级页面，包含大盘监控指标、双模式新建向导、微信通道配置弹窗与运行历史日志抽屉。

**Tech Stack:** C23, SQLite 3, PostgreSQL 16, csilk HTTP framework, Unified AI Runtime, Vue 3, TypeScript, Vite, Element Plus, Pinia.

**Spec:** [docs/superpowers/specs/2026-11-18-scheduler-center-design.md](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-18-scheduler-center-design.md)

---

## Global Constraints

- **C23 & 零编译器警告**：后端代码使用 C23 标准，`-Wall -Wextra -Werror` 零编译告警。
- **DDD 四层架构规范**：严格单向依赖：Controller → Application → Domain / Infrastructure / Core。用例函数入参统一使用 `void* pool` 刻意解耦设计。
- **双引擎 SQL 兼容性**：所有数据库迁移与仓储实现必须同时兼容 SQLite 与 PostgreSQL，所有 SQL 使用 `?` 占位符绑定，写操作使用 `csilk_db_exec_param`，避免堆分配遗留。
- **内存受限与沙箱隔离**：所有构建与测试执行必须使用 `./backend/tests/run_isolated.sh`（cgroups v2 `MemoryMax=2G/4G`, ASan RSS limit 2048MB），严禁在无内存限制下并发编译消耗主机 RAM。
- **前端零类型错误**：前端修改必须通过 `vue-tsc -b` 与 `npm --prefix frontend test` 验证。

---

## Review Focus

1. **多用户与系统级底座任务权限越权**：非管理员普通用户尝试修改、删除或暂停 `user_id = 0` 的系统级底座任务时，必须返回 `code 1004 (Forbidden)`，且列表接口仅对管理员返回 `user_id = 0` 的系统任务。
2. **高频行情异动事件防刷轰炸**：当持仓标的涨跌幅超过设定阈值（如 ±3%）时，必须具备边缘触发（Edge Trigger）与冷却窗口（默认 240 分钟或当前交易日内已触发过不再重复推送），防止轮询导致数分钟内多次重复推送到微信。
3. **长耗时 AI 提示词任务超时阻断**：AI 任务（带联网搜索与大模型推理）在 Worker 线程池中执行时，必须严格受控于 60 秒硬超时和 Token 预算守卫，超时后状态记为 `timeout`，不得无限期霸占 Worker 线程。
4. **微信推送网络故障容错与日志记录**：当个人微信推送通道网络不通或 SendKey 错误导致 HTTP 失败时，任务主体数据计算必须保留，日志表记录 `status='success'` 但 `notification_status='failed'` 及具体错误原因。
5. **任务跨月与闰年 Cron 周期计算**：Cron 解析器在计算每月最后一天、2 月 29 日或跨年份周期间隔时，时间计算不得出现死循环或负时间戳偏移。

---

## Task Decomposition

### Task 1: 数据库迁移脚本与表结构（Flyway V029）

**Files:**
- Create: `backend/sql/migrations/sqlite/V029__scheduled_jobs_and_logs.sql`
- Create: `backend/sql/migrations/postgres/V029__scheduled_jobs_and_logs.sql`
- Test: `backend/tests/unit/test_scheduler_migration.c`

**Interfaces:**
- Produces: 
  - `scheduled_jobs` 表（包含 `user_id`, `job_type`, `trigger_type`, `trigger_config`, `payload_config`, `delivery_channel`, `delivery_config`, `enabled`, `next_run_at`）
  - `user_notification_channels` 表（包含 `user_id`, `channel_type`, `webhook_url`, `secret_key`, `is_active`）
  - `job_run_logs` 表（包含 `job_id`, `user_id`, `started_at`, `completed_at`, `status`, `duration_ms`, `trigger_reason`, `output_summary`, `error_message`, `notification_status`）

- [x] **Step 1: 编写 SQLite 与 PostgreSQL 的 V029 迁移 SQL 文件**
  - 在 `backend/sql/migrations/sqlite/V029__scheduled_jobs_and_logs.sql` 中使用 `INTEGER PRIMARY KEY AUTOINCREMENT`。
  - 在 `backend/sql/migrations/postgres/V029__scheduled_jobs_and_logs.sql` 中使用 `BIGSERIAL PRIMARY KEY`。
  - 建立索引 `idx_scheduled_jobs_user_enabled`、`idx_job_run_logs_job_id`、`idx_job_run_logs_user_started`。

- [x] **Step 2: 编写迁移单元测试 `test_scheduler_migration.c`**
  - 在内存/临时 SQLite 库中执行迁移并验证表结构存在、列约束有效。
  - 验证 `user_notification_channels` 的 `UNIQUE(user_id)` 约束。

- [x] **Step 3: 隔离环境编译并运行迁移测试**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- ./backend/build/test_scheduler_migration`
  - Expected: PASS

- [x] **Step 4: 提交 Task 1 代码**
  - Git commit: `feat(db): add V029 migration for scheduled jobs and run logs`

---

### Task 2: 领域实体与 DDD 仓储实现（`scheduler_repo_impl`）

**Files:**
- Create: `backend/src/domain/scheduler/entity.h`
- Create: `backend/src/domain/scheduler/repository.h`
- Create: `backend/src/infrastructure/repositories/scheduler_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/scheduler_repo_impl.c`
- Test: `backend/tests/unit/test_scheduler_repo.c`

**Interfaces:**
- Produces:
  - `csilk_json_t* mf_scheduler_repo_job_list(void* pool, int64_t user_id, bool is_admin, int64_t page, int64_t page_size, int64_t* out_total)`
  - `csilk_json_t* mf_scheduler_repo_job_get(void* pool, int64_t user_id, bool is_admin, int64_t job_id)`
  - `int64_t mf_scheduler_repo_job_create(void* pool, int64_t user_id, const char* name, const char* desc, const char* job_type, const char* trigger_type, const char* trigger_cfg, const char* payload_cfg, const char* delivery_channel, const char* delivery_cfg, const char* next_run_at)`
  - `int mf_scheduler_repo_job_update(void* pool, int64_t user_id, bool is_admin, int64_t job_id, const char* name, const char* desc, const char* trigger_type, const char* trigger_cfg, const char* payload_cfg, const char* delivery_channel, const char* delivery_cfg, const char* next_run_at)`
  - `int mf_scheduler_repo_job_delete(void* pool, int64_t user_id, int64_t job_id)`
  - `int mf_scheduler_repo_job_set_enabled(void* pool, int64_t user_id, bool is_admin, int64_t job_id, bool enabled)`
  - `int mf_scheduler_repo_job_update_last_run(void* pool, int64_t job_id, const char* status, int duration_ms, const char* next_run_at)`
  - `csilk_json_t* mf_scheduler_repo_get_user_channel(void* pool, int64_t user_id)`
  - `int mf_scheduler_repo_save_user_channel(void* pool, int64_t user_id, const char* channel_type, const char* channel_name, const char* webhook_url, const char* secret_key)`
  - `int64_t mf_scheduler_repo_log_create(void* pool, int64_t job_id, int64_t user_id, const char* trigger_reason)`
  - `int mf_scheduler_repo_log_complete(void* pool, int64_t log_id, const char* status, int duration_ms, const char* output_summary, const char* error_msg, const char* notif_status)`
  - `csilk_json_t* mf_scheduler_repo_log_list(void* pool, int64_t user_id, bool is_admin, int64_t job_id, int64_t page, int64_t page_size, int64_t* out_total)`

- [x] **Step 1: 编写失败的仓储单元测试 `test_scheduler_repo.c`**
  - 测试创建作业、更新状态、用户隔离查询（普通用户查不到他人作业）、保存和读取用户微信通道配置、记录运行日志等。

- [x] **Step 2: 实现 `domain/scheduler/` 契约与 `scheduler_repo_impl.c`**
  - 所有 SQL 使用 `?` 参数化绑定，写操作使用 `csilk_db_exec_param`，避免堆泄漏。
  - 普通用户只查 `user_id = ?`，管理员可查 `(user_id = ? OR user_id = 0)`。

- [x] **Step 3: 运行仓储单元测试并验证通过**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- ./backend/build/test_scheduler_repo`
  - Expected: PASS

- [x] **Step 4: 提交 Task 2 代码**
  - Git commit: `feat(scheduler): implement domain entities and repository layer`

---

### Task 3: 纯 C 标准 5 段式 Cron 解析器与周期推算（`cron_parser`）

**Files:**
- Create: `backend/src/core/scheduler/cron_parser.h`
- Create: `backend/src/core/scheduler/cron_parser.c`
- Test: `backend/tests/unit/test_cron_parser.c`

**Interfaces:**
- Produces:
  - `int cron_parse_expr(const char* expr, cron_expr_t* out_parsed)`
  - `time_t cron_next_after(const cron_expr_t* parsed, time_t after_time)`
  - `int cron_format_next_str(const char* expr, time_t after_time, char* out_buf, size_t buf_len)`

- [x] **Step 1: 编写 Cron 解析器单元测试 `test_cron_parser.c`**
  - 涵盖：每分钟 `* * * * *`、工作日 05:00 `0 5 * * 1-5`、步长 `*/15 * * * *`、多项逗号 `0 8,12,18 * * *`、月末/闰年跨度推算。

- [x] **Step 2: 实现 `cron_parser.c`**
  - 纯标准 C 实现，解析 5 个字段（分钟 0-59、小时 0-23、日 1-31、月 1-12、周 0-7），基于 `struct tm` 进行下一有效时间步进。

- [x] **Step 3: 运行 Cron 单元测试验证**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- ./backend/build/test_cron_parser`
  - Expected: PASS

- [x] **Step 4: 提交 Task 3 代码**
  - Git commit: `feat(scheduler): implement pure C standard 5-part cron parser`

---

### Task 4: 个人微信通用推送适配服务（`wechat_push_service`）

**Files:**
- Create: `backend/src/services/notification/wechat_push_service.h`
- Create: `backend/src/services/notification/wechat_push_service.c`
- Test: `backend/tests/unit/test_wechat_push_service.c`

**Interfaces:**
- Produces:
  - `typedef struct { char channel_type[32]; char webhook_url[256]; char secret_key[128]; } push_channel_config_t;`
  - `int wechat_push_service_send(const push_channel_config_t* cfg, const char* title, const char* markdown_body, char* err_buf, size_t err_len)`

- [x] **Step 1: 编写测试用例 `test_wechat_push_service.c`**
  - 启动本地 Mock HTTP 服务器，验证 Server酱协议（`title`, `desp` 参数）、PushDeer 协议（`text`, `desp`, `type=markdown`）与通用 JSON Webhook 协议的请求构造与返回校验。

- [x] **Step 2: 实现 `wechat_push_service.c`**
  - 使用 `curl_easy_*` 发送 HTTPS POST，设置 10 秒超时，返回标准错误诊断。

- [x] **Step 3: 编译并运行推送适配测试**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- ./backend/build/test_wechat_push_service`
  - Expected: PASS

- [x] **Step 4: 提交 Task 4 代码**
  - Git commit: `feat(notification): implement wechat push service for serverchan, pushdeer, and webhooks`

---

### Task 5: 核心调度引擎、事件总线与 Worker 线程池（`core/scheduler/`）

**Files:**
- Create: `backend/src/core/scheduler/scheduler_worker_pool.h/.c`
- Create: `backend/src/core/scheduler/scheduler_events.h/.c`
- Create: `backend/src/core/scheduler/scheduler_engine.h/.c`
- Modify: `backend/src/services/market/quote_engine.c` (注入闭市与价格异动事件钩子)
- Modify: `backend/src/main.c` (初始化启动与停止)
- Test: `backend/tests/unit/test_scheduler_engine.c`

**Interfaces:**
- Produces:
  - `int scheduler_engine_start(void* pool)`
  - `void scheduler_engine_stop(void)`
  - `int scheduler_engine_trigger_job_now(int64_t job_id, int64_t user_id, const char* reason)`
  - `void scheduler_event_publish_price_spike(int64_t asset_id, const char* symbol, double change_pct)`
  - `void scheduler_event_publish_market_closed(const char* market_code)`

- [x] **Step 1: 编写引擎集成与事件触发单元测试 `test_scheduler_engine.c`**
  - 模拟作业注册、时钟到期入池执行、边缘触发事件（异动超过阈值触发一次，第二次在冷却期内被拦截）。

- [x] **Step 2: 实现 `scheduler_worker_pool.c` 与 `scheduler_events.c`**
  - 固定 3 个 Worker 线程，队列基于互斥锁与条件变量，支持 60s 执行超时取消。
  - 事件总线维护最近触发时间戳字典，实现 240 分钟防刷冷却。

- [x] **Step 3: 实现 `scheduler_engine.c`**
  - 启动时加载活跃任务，维护时钟最小堆，循环使用 `pthread_cond_timedwait` 挂起与精确唤醒。
  - AI 任务解析 `payload_config`，构建持仓列表上下文，调用 `ai_runtime_execute`。
  - 任务完成后统一调用 `wechat_push_service_send` 并通过仓储更新运行日志。

- [x] **Step 4: 运行引擎单元测试**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- ./backend/build/test_scheduler_engine`
  - Expected: PASS

- [x] **Step 5: 提交 Task 5 代码**
  - Git commit: `feat(scheduler): implement core scheduler engine, worker pool, and event bus`

---

### Task 6: 应用用例层与 HTTP 控制器（REST API）

**Files:**
- Create: `backend/src/application/scheduler/dtos.h`
- Create: `backend/src/application/scheduler/usecases.h`
- Create: `backend/src/application/scheduler/usecases.c`
- Create: `backend/src/interfaces/http/controllers/scheduler_controller.h`
- Create: `backend/src/interfaces/http/controllers/scheduler_controller.c`
- Modify: `backend/src/main.c` (注册 `/api/scheduler/*` 路由)
- Test: `backend/tests/test_scheduler.sh`

**Interfaces:**
- Produces:
  - 对应 Spec 4.3 节的全部 12 个 REST 接口端点，集成身份认证、多租户归属校验与管理员权限守卫。

- [x] **Step 1: 编写集成测试脚本 `backend/tests/test_scheduler.sh`**
  - 覆盖系统概览查询、任务创建、普通用户越权修改他人任务被拦截（1004）、非管理员启停系统内置任务被拦截（1004）、立即触发执行、微信通道配置保存与测试发送、历史日志分页查询。

- [x] **Step 2: 实现用例层 `usecases.c` 与控制器 `scheduler_controller.c`**
  - 绑定 JSON 请求参数，调用 DDD 仓储层，将成功结果封装为 `respond_ok(c, data)`。

- [x] **Step 3: 在 `main.c` 中注册路由并在隔离环境中运行端到端测试**
  - Run: `./backend/tests/run_isolated.sh build backend/build`
  - Run: `./backend/tests/run_isolated.sh -- bash backend/tests/test_scheduler.sh`
  - Expected: ALL ASSERTIONS PASS

- [x] **Step 4: 提交 Task 6 代码**
  - Git commit: `feat(api): implement scheduler use cases and http controller`

---

### Task 7: 前端类型、API 封装、路由与左侧菜单集成

**Files:**
- Create: `frontend/src/types/scheduler.ts`
- Modify: `frontend/src/types/index.ts`
- Create: `frontend/src/api/scheduler.ts`
- Modify: `frontend/src/router/index.ts`
- Modify: `frontend/src/views/Layout.vue`
- Modify: `frontend/src/utils/locale.ts`

- [x] **Step 1: 声明 TypeScript 接口与 API 方法**
  - `ScheduledJob`, `JobRunLog`, `SchedulerDashboard`, `UserPushChannel` 类型定义。
  - `schedulerApi` 封装完整 REST 请求。

- [x] **Step 2: 注册路由并在左侧导航“系统”组增加菜单入口**
  - 在 `router/index.ts` 注册 `/scheduler` 指向 `Scheduler.vue`。
  - 在 `Layout.vue` 增加导航项及多语言键值。

- [x] **Step 3: 运行前端类型检查**
  - Run: `npm --prefix frontend run build`
  - Expected: 无类型错误编译通过

- [x] **Step 4: 提交 Task 7 代码**
  - Git commit: `feat(frontend): add scheduler types, api client, router and navigation`

---

### Task 8: 前端管理大盘、新建向导、微信配置与日志抽屉

**Files:**
- Create: `frontend/src/views/Scheduler.vue`
- Create: `frontend/src/components/scheduler/JobDialog.vue`
- Create: `frontend/src/components/scheduler/ChannelConfigDialog.vue`
- Create: `frontend/src/components/scheduler/JobLogDrawer.vue`
- Test: `npm --prefix frontend test` & `npm --prefix frontend run build`

- [ ] **Step 1: 实现 `ChannelConfigDialog.vue` 微信通道配置组件**
  - 支持选择通道类型（Server酱 / PushDeer / 企业微信代推 / Webhook），输入密钥，并带有“发送测试消息”按钮。

- [ ] **Step 2: 实现 `JobDialog.vue` 新建/编辑作业弹窗**
  - 顶部 Tab 切换：`AI 智能任务 (Prompt 驱动)` 与 `结构化报表任务 (表单驱动)`。
  - 频次配置：提供直观的常用周期快捷选择（工作日收盘后、每天指定时刻等）与 Cron 表达式输入框。
  - AI 任务项：持仓上下文注入开关（带脱敏配置）、联网搜索开关。

- [ ] **Step 3: 实现 `JobLogDrawer.vue` 运行日志抽屉**
  - 展示任务最近 30 天运行记录，卡片展开显示推送至微信的 Markdown 简报内容或执行报错详情。

- [ ] **Step 4: 实现 `Scheduler.vue` 主面板**
  - 顶部 4 项运行指标卡片，主区域任务表格，包含状态开关、立即执行、编辑与日志操作按钮。

- [ ] **Step 5: 前端测试与生产构建验证**
  - Run: `npm --prefix frontend test`
  - Run: `npm --prefix frontend run build`
  - Expected: 0 失败，0 告警，生产 bundle 打包完成。

- [ ] **Step 6: 提交 Task 8 代码**
  - Git commit: `feat(frontend): implement scheduler dashboard, job dialog, and log drawer`

---

### Task 9: 全流程端到端集成测试与双引擎全量回归

**Files:**
- Modify: `backend/tests/run_isolated.sh` (将 `test_scheduler.sh` 纳入 `integration` 测试列表)

- [ ] **Step 1: 运行全量隔离环境单元测试 (75+ 项)**
  - Run: `./backend/tests/run_isolated.sh unit backend/build`
  - Expected: 100% tests passed, 0 failures.

- [ ] **Step 2: 运行全量端到端隔离集成测试 (15 套)**
  - Run: `./backend/tests/run_isolated.sh integration backend/build`
  - Expected: 包含 `test_link.sh`, `test_scheduler.sh` 在内的全部测试套件 100% 通过。

- [ ] **Step 3: 提交 Task 9 代码与实施计划勾选完成**
  - Git commit: `test(scheduler): integrate scheduler into regression suites and verify dual engines`
