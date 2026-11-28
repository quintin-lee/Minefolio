# Minefolio — 系统架构设计文档（v1.4.1 现状）

> **状态**：当前有效。本文描述仓库 HEAD 的真实实现，已按 v1.4.1 代码原地重写。
> **历史说明**：本文件最初是 2026-08-10 的 MVP 设计稿（单体 `auth.c/categories.c`、cJSON、固定单用户、无 AI / 无移动端）。该版本早已落地并被重构取代，阅读本文即可了解现状；单次功能演进的历史记录仍保留在 `docs/superpowers/{specs,plans}/` 的其他文件中。
> **受众**：研发、运维、安全审计、二次开发者。

---

## 1. 项目概述

### 1.1 定位

Minefolio 是一款自托管的**全资产管理与投资追踪系统**：统一管理现金、银行、证券、基金、债券、加密货币、房产等资产与贷款、信用卡等负债，覆盖日常收支记账、持仓成本与盈亏核算、多账本协作，并提供 AI 财务助理（对话式记账 / 多步分析工作流）、MCP 工具生态、行情定时同步、调度推送与移动端离线使用。

### 1.2 技术栈

| 层次 | 选型 | 说明 |
|------|------|------|
| 后端语言 | C23 | GCC 14 / Clang 18，`-Wall -Wextra -Wpedantic` |
| Web 框架 | csilk v1.2.0 | FetchContent 拉取（本地 `deps/csilk` 存在时优先），见 `backend/CMakeLists.txt:16-31` |
| 数据库 | SQLite（WAL，默认） / PostgreSQL 16+（可选） | `libpq` 可用时编译 `HAVE_LIBPQ`，双方言迁移脚本对等维护 |
| JSON | yyjson | 高性能 DOM / mutable DOM |
| 其他 C 库 | libcurl、OpenSSL crypto、pthread | curl 全局初始化在线程创建前完成 |
| 前端 | Vue 3.4 + TypeScript 5.5 + Vite 5 | Element Plus + Iconify、Pinia、vue-router、axios、ECharts 5、vue-i18n |
| 移动 | Capacitor 6 + sql.js (WASM) | 独立入口 `main-mobile.ts`，本地 SQLite 离线库 + 联网同步 |
| 测试 | CTest（后端）+ Vitest（前端） | 后端可选 ASan+UBSan 构建 |

### 1.3 代码规模（以当前仓库为准）

| 指标 | 数值 |
|------|------|
| 后端 `.c` / `.h` | 234 / 301 |
| HTTP 控制器 | 28 |
| 领域（domain） | 21 个域 |
| 仓储实现 | 28 |
| 数据库迁移 | 38 个版本（SQLite / PostgreSQL 各 38 份），约 45 张表 |
| HTTP API 路径 | 162 个去重后的 `/api/*` 路径 |
| AI 内置工具 | 25 个注册项（含 2 个内部确认工具）+ 动态 MCP 工具 |
| AI 多步工作流 | 13 个内置 DAG |
| 单元测试目标 | 约 87 个（67 个宏调用 + 20 个显式 `add_test`） |

---

## 2. 系统拓扑与请求链路

### 2.1 部署拓扑

```
浏览器 (Vue 3 SPA) ─┐
                    ├─HTTPS─> Nginx :80 ──/api/* 反代──> csilk :8080 ──> SQLite / PostgreSQL
移动 App (Capacitor)┘              └──静态资源── frontend/dist
                                                            │
                                                            ├─> LLM Provider (OpenAI 兼容 / DeepSeek)
                                                            ├─> 行情源 (EastMoney / Tencent / Yahoo / Crypto)
                                                            └─> MCP Server (stdio / HTTP) · IM 网关 (gateway/)
```

- 开发期：Vite `:5173` 通过 proxy 将 `/api` 转发至 `:8080`，见 `frontend/vite.config.ts:33-42`。
- 生产期：Nginx 托管静态资源并反代 API；csilk 亦内置静态文件服务作为回退（`backend/src/main.c:213-221`）。
- 移动端可配置自建服务端地址，断网时使用本地 `sql.js` 库，联网后再同步。

### 2.2 一次典型请求的链路

```
HTTP 请求
  → 中间件链（recovery / logger / request-id / metrics / 安全头 / 认证限流 / CORS / JWT / CSRF）
  → Controller（参数提取、调用 usecase、响应封装）
  → Application usecase（编排领域规则、财务原语、账本引擎）
  → Domain（实体 + rules 纯规则，仓储接口）
  → Infrastructure（仓储实现执行 SQL）
  → SQLite / PostgreSQL
```

WebSocket 未使用；AI 流式对话与工作流运行使用 SSE（`text/event-stream`），路径为 `/api/ai/chat` 与 `/api/ai/workflows/run`。

### 2.3 进程内后台线程

- **csilk worker 线程池**：默认 2，可用 `MINEFOLIO_WORKERS` 调整（1–16），见 `backend/src/main.c:105-112`。
- **行情调度器**：`market_scheduler_start(pool)` 定时拉取行情与汇率。
- **AI 定时任务引擎**：`scheduler_engine_start(pool)`，含 5 段式 Cron 解析、动态休眠工作池与内置任务。
- **异步任务管理器**：`task_controller_init(pool, 4)`，处理行情批量同步、备份、重算、OCR、导入等长任务。

优雅停机（SIGTERM/SIGINT）依次停止 server、调度器、任务管理器、AI 新闻服务，再做 WAL checkpoint 与资源释放（`backend/src/main.c:58-68`、`241-255`）。

---

## 3. 后端架构

### 3.1 分层与依赖方向

```
Interfaces (HTTP) → Application (Use Cases) → Domain (Entities + Rules) → Infrastructure (SQL Repos)
                                                      ↑
                          Financial Core (decimal_t / money_t / … ) + Ledger Engine (事件溯源)
```

严格单向依赖：Domain 层零外部依赖，定义实体、仓储契约与纯规则；Infrastructure 实现仓储；Financial Core 与 Ledger Engine 作为被 Application / Domain 复用的能力核心。

### 3.2 目录结构（`backend/src/`）

```
main.c                     进程入口：初始化、中间件、路由、后台线程、优雅停机
interfaces/http/controllers/  28 个控制器，使用 csilk_app_{get,post,put,delete}_ext 注册路由
application/<domain>/         usecases.c / commands.h / dtos.h（21 个域）
domain/<domain>/              entity.h / repository.h / rules.c（纯业务规则）
infrastructure/
  repositories/              28 个 *_repo_impl.c，实现领域仓储契约
  database/                  database.h/.c、statement、transaction、sqlite/、postgres/、db_backup
core/
  financial/                 decimal（__int128 定点）、money、quantity、price、rate、percentage、currency
  ledger/                    ledger_engine、ledger_math、ledger_rebuild、ledger_types
  scheduler/                 cron_parser、scheduler_engine、scheduler_worker_pool、scheduler_events
services/
  ai/                        runtime / model / tools / policy / workflow / workflows / trace / memory / mcp
  market/                    quote_engine、exchange_rate_service、driver_*、market_scheduler
  report/、notification/、file_parser.c、import_service.c、export_service.c
common/                      db、jwt、response、ctx、sse、balance、ledger_utils、tx_types、http_safety、ai_config、ai_trace …
config/                      secret.c（统一敏感配置门禁）、key_manager.c（RSA 密钥）、db_config.c
middlewares/                 jwt、cors、csrf、security_headers、rate_limit、gateway_auth
dtos/                        请求 / 响应结构
```

### 3.3 启动流程与中间件顺序（`backend/src/main.c`）

1. 忽略 SIGPIPE；`curl_global_init`（必须在线程创建前）；校验 `JWT_SECRET`（拒绝默认值 / 空值）。
2. `db_init` → `db_run_migrations`；`auth_key_init` 生成/加载 RSA-2048 密钥；`ai_init`；`task_controller_init(pool, 4)`。
3. 创建 `csilk_app_t`，设置 worker 数；按序注册中间件：
   `csilk_recovery_handler` → `csilk_logger_handler` → `csilk_request_id_middleware` → `metrics` → `security_headers` → `rate_limit_auth`（`/api`）→ `cors` → `jwt`（`/api` 与 `/mcp`）→ `mcp_rate_limit`（`/mcp`）→ 可选 `csrf`（`ENABLE_CSRF`）。
4. `/healthz` 与 CORS preflight；注册 28 组业务路由；`/csilk-admin` 管理后台；静态资源回退。
5. 注册 iLink 推送钩子；启动行情调度器与定时引擎；监听端口（`PORT` / `MINEFOLIO_PORT`，默认 8080）。

CORS 未设置 `CORS_ORIGIN` 时返回 `*` 且禁止携带凭证，并打印警告（`main.c:154-162`），避免 `*` 与 credentials 共存。

### 3.4 业务域与 API 面

21 个域：`account, ai, asset, auth, cashflow, category, daily_expense, dca, file, import_export, ledger, market, mcp, portfolio, report, scheduler, skill, tag, task, transaction, transfer`。

控制器分组（路由数量总计 162，以下为路径族摘要）：

| 控制器 | 主要路径族 |
|--------|-----------|
| `auth_controller` | `/api/auth/{register,login,me,password,public-key,2fa/*,oauth/*}` |
| `admin_controller` | `/api/admin/users/*`、`/api/system/{setup,status,backup/*}` |
| `account_controller` | `/api/accounts/*`、`/api/ledgers/:id/accounts/:id/permissions` |
| `asset_controller` | `/api/assets/*`、`/api/assets/:id/rebuild`、`/api/asset-balance-logs` |
| `category_controller` | `/api/categories/*` |
| `transaction_controller` | `/api/transactions/*`、`/api/transactions/monthly` |
| `transfer_controller` | `/api/transfers` |
| `daily_expense_controller` | `/api/daily-expenses/*` |
| `tag_controller` | `/api/tags/*` |
| `ledger_controller` | `/api/ledgers/*`、`/api/ledgers/join` |
| `report_controller` | `/api/reports/{expense,asset,transaction}/*`、`/api/summary`、`/api/reports/{holdings,position-overview,fx-pnl,currency-summary,multi-currency-summary,yearly}` |
| `portfolio` / `cashflow` / `dca` | `/api/cashflow/*`、`/api/dca/*` |
| `market_controller` | `/api/market/{quote,search,history,indices,sync,exchange-rates,fx-rates,fx-history,settings,test-proxy}` |
| `ai_controller` | `/api/ai/{chat,models,sessions,workflows,actions/confirm*}`、`/api/settings/ai*` |
| `ai_mcp_controller` / `mcp_server_controller` | `/api/ai/mcp/*`、对外 `/mcp` JSON-RPC 服务 |
| `skill_controller` / `ai_news_controller` / `ai_trace_controller` | `/api/ai/skills/*`、`/api/ai/news/*`、`/api/ai/traces/*` |
| `scheduler_controller` / `task_controller` | `/api/scheduler/*`、`/api/tasks/*` |
| `im_bot_controller` / `im_bot_instance_controller` | `/api/im/*`、`/api/gateway/im/instances/*` |
| `file_controller` / `receipt_controller` | `/api/files/upload`、`/api/receipts/scan` |
| `import_export_controller` / `import_rule_controller` | `/api/import/*`、`/api/export/*`、`/api/import-rules/*` |

统一响应信封：`{ "code": 0, "message": "ok", "data": ... }`；错误码 1001 未授权、1002 参数错误、1003 不存在、1004 已存在等。统一定义在 `backend/src/common/response.h` 与 `dtos/response.h`。

### 3.5 财务核心与账本引擎

- **Financial Core**（`core/financial/`）：基于 64 位定点数与 `__int128` 扩展精度的 `decimal_t`，派生强类型 `money_t / quantity_t / price_t / rate_t / percentage_t`，避免浮点误差；仅在 JSON DTO 边界回落为 `double`。`decimal.c` 因 `__int128` 按文件豁免 `-Wpedantic`（`backend/CMakeLists.txt:63-64`）。
- **Ledger Engine**（`core/ledger/`）：以「Transaction 是唯一金融事实」为原则，提供原子 `ledger_apply_tx` / `ledger_reverse_tx`，统一计算持仓、现金余额、成本基础、已实现/浮动盈亏；支持全量重建与基于 `ledger_snapshots` 检查点的增量重建，回溯改账级联失效下游快照；交易落库固化 Point-in-Time 汇率。
- **持仓与成本**：`domain/asset/` 提供 FIFO / 加权均价成本基础（`transaction_lots`，V021）、PnL、性能与持仓模型；高精度量价存储由 V032 提供。
- **余额方向**：`common/balance.c` 对 `loan / credit_card / other_liability` 做符号翻转，保证 `net_worth = assets - liabilities`。

### 3.6 AI 子系统

```
services/ai/
  runtime/    统一 Agent 循环（流式 + 同步）、context、session、limits、error、fast_json、loop_*
  model/      provider 适配、request / response、模型探测
  tools/      25 个内置注册项（11 个 register_all 模块）+ MCP 客户端 / stdio 池 / 桥接 / 配置
  policy/     permission、risk（五级）、confirmation（双重确认 + nonce 防重放）、audit
  workflow/   DAG 图执行器：graph / node / executor / context / engine
  workflows/  13 个内置工作流（见下）
  trace/      span / trace / exporter，端到端追踪与成本统计
  memory/     滑动窗口记忆、摘要、系统提示词保护
```

- **Agent 循环**：`ai_runtime_execute_stream` 通过回调暴露文本块、thinking 块、工具调用/结果、错误、完成、消息落库事件（`services/ai/runtime/loop.h:13-41`）；`ai_runtime_execute` 为同步版本。
- **工具注册表**：`tools/registry.c` 维护最多 64 个工具，区分只读 / 变更工具快照；`internal_only` 工具（如 `confirm_proposed_expense`、`confirm_proposed_transfer`）**绝不暴露给 LLM**，防止提示注入绕过人工确认（`registry.c:28-59`）。11 个注册模块：asset、transaction、expense、transfer、cashflow、portfolio、report、news、web_search、batch、skill。另有 `list_skills` / `use_skill` 使用 Agent Skills（V022）。
- **13 个内置工作流**：`wf_monthly_review`、`wf_portfolio_rebalance`、`wf_expense_decision`、`wf_payday_split`、`wf_budget_guard`、`wf_anomaly_detect`、`wf_subscription_audit`、`wf_emergency_fund`、`wf_goal_tracker`、`wf_debt_payoff`、`wf_cashflow_forecast`、`wf_bill_calendar`、`wf_health_score`，注册于 `services/ai/workflow/engine.c:17-35`。工作流支持条件分支与 Saga 补偿。
- **风控**：五级风险评估 + 带 nonce 的确认令牌；已消费 nonce 持久化（`ai_consumed_nonces`，V019），重放防护跨进程重启生效。
- **提示注入防御**：对外部工具内容做 XML 隔离与闭合标签转义；剥离 Markdown 图片链接以防数据外带；提供系统级 guardrail prompt（`loop.h:93-106`）。
- **可观测性**：AI 会话、消息、thinking 内容（V015）、工具调用（V016）、trace 与统计落库，前端有 `AiTraces` / `AiTraceDetail` 页面。

### 3.7 行情、调度、异步任务与通知

- **行情**：`services/market/` 下 EastMoney / Tencent / Yahoo / Crypto / mock 驱动，`quote_engine` 编排，`exchange_rate_service` 处理汇率与手动锁定标记（V026），`market_scheduler` 定时同步。
- **调度中心**：`core/scheduler/` 提供 5 段式 Cron 解析、动态休眠 worker pool、事件通知；内置任务由 V030 播种；可通过 `/api/scheduler/*` 管理并即时触发；支持 iLink outbox 推送（V038）。
- **异步任务**：`application/task/async_task_manager.c` + `async_tasks` 表（V033），提供进度追踪与取消。
- **通知**：`services/notification/wechat_push_service.c` 与 Webhook 推送。

### 3.8 数据模型与迁移

- 迁移脚本位于 `backend/sql/migrations/{sqlite,postgres}/V<NNN>__<name>.sql`，当前 **V001~V038 共 38 个版本**，双方言语义对齐；启动时由 `db_run_migrations` 经 csilk 迁移引擎应用。
- 版本涵盖：认证与系统、分类与资产、交易与收支、账本与成员、AI trace/设置、DCA/现金流、行情与 FX、账本作用域分类、储蓄目标、AI 会话摘要、MCP Server、转账级联、市场指数、显式转账交易、thinking/工具调用、账户表、nonce 防重放、账本快照与 Point-in-Time FX、成本方法/批次、AI 技能、报表索引、AI 财经早报、回溯回填、汇率手动标记、IM 绑定、定时 jobs、账户/字段 RBAC、量价精度、异步任务、用户管理、IM 实例、分类去重与索引、推送 outbox。
- 多租户：`ledgers` + `ledgers_members`（Owner / Editor / Viewer）实现账本隔离；`X-Ledger-Id` 头贯穿请求链路；账户级/字段级 RBAC 由 V031 提供；移动端本地库同样按 `ledger_id` 隔离。

---

## 4. 前端架构

### 4.1 桌面 SPA（`frontend/src/`）

- **入口/路由**：`main.ts`、`App.vue`、`router/index.ts`；Setup / Login / OAuth Callback 免鉴权，Layout 下挂 18 个子页面（Dashboard、Assets、AssetDetail、Holdings、Transactions、DailyExpenses、Plans、Categories、Reports、Market、Scheduler、AuditLogs、Settings、Chat、Skills、Mcp、News、AiTraces）。
- **状态**：Pinia stores：`auth`、`ledger`、`category`、`chat`、`market`、`sync`、`theme`、`display-settings`。
- **API 层**：`api/*.ts` 约 24 个模块，统一经由 `utils/http.ts`：解包 `{code,message,data}` 信封、注入 JWT / `X-Ledger-Id` / CSRF Token、1001 统一登出、错误防抖 Toast、自定义服务端地址。
- **组件**：约 120 个 `.vue`（视图 + 组件），含市场面板、收支图表、资产走势、费用饼图、账本/账户对话框、MCP / Skills / Scheduler 管理、票据 OCR 等。
- **i18n**：`vue-i18n`，`locales/zh-CN.ts` 与 `en-US.ts`，`useI18n` composable；默认简体中文。
- **构建**：Vite 手动分包（`vendor-echarts` / `vendor-element` / `vendor-markdown` / `vendor-hljs`），`vue-tsc -b` 严格类型检查，Vitest 测试。

### 4.2 移动端（`frontend/src/main-mobile.ts` 等）

- Capacitor 6 壳（Android / iOS），`vite.config.mobile.ts` 独立构建，`views-mobile/` 专用视图与 `MobileLayout.vue`。
- `db/local.ts` + `db/schema.ts` 使用内嵌 base64 的 `sql.js` WASM 运行本地 SQLite，支持 WebCrypto 非导出密钥加密；`stores/sync.ts`、`utils/offline-http.ts`、`utils/swr-cache.ts` 负责离线读写与联网同步。
- 支持自建服务端地址动态配置与延迟探测（`components/mobile/ServerConfigDialog.vue`、`utils/server-url.ts`）。

### 4.3 AI 工作台渲染管线

`POST /api/ai/chat` 与 `/api/ai/workflows/run` 返回 SSE；前端 `Chat.vue` 通过 `fetch` + `ReadableStream` 读取事件，经 typewriter 缓冲平滑输出；Markdown 由 `marked` + DOMPurify 净化，`highlight.js` 做代码高亮，`MermaidBlock.vue` 渲染工作流 DAG；支持财务实体悬停卡片、`@` 账户提及、批量动作卡片与金额一键掩码。

---

## 5. 安全设计

| 领域 | 措施 |
|------|------|
| 认证 | JWT（HS256）从环境变量读取 `JWT_SECRET`，启动强制校验；支持令牌版本号即时吊销、TOTP 2FA、GitHub/通用 OIDC 登录 |
| 密码传输与存储 | 前端 RSA-OAEP 加密口令；后端 RSA-2048 + bcrypt（含 legacy 兼容）；`reset-password` 运维 CLI |
| 写入防护 | 认证写接口与 `/mcp` 限流；CSRF（可选，Cookie `csrf_token` + `X-CSRF-Token`）；CORS 白名单；安全响应头 |
| SQL | 全部参数化查询，禁止字符串拼接 |
| 多租户 | `ledger_id` 隔离 + 账户/字段 RBAC；报表、汇总、AI 工具均按视野过滤与脱敏 |
| AI | 五级风控、双重确认 + nonce 防重放、internal-only 工具隔离、提示注入防御、Markdown 图片剥离 |
| 运维 | 统一 Secret Provider（环境变量 / 文件挂载 / Vault 等），生产启动期拒绝硬编码弱口令；SQLite 在线热备份与 WAL checkpoint |

---

## 6. 构建、测试与部署

- **后端**：`cmake -S backend -B backend/build && cmake --build backend/build`；可选 `-DMINEFOLIO_SANITIZE=ON` 启用 ASan+UBSan。构建产物含 `minefolio`、静态库 `minefolio_core`、`reset-password` CLI，并自动复制 `config/` 与 `sql/`。
- **前端**：`npm ci && npm run build`（`vue-tsc -b && vite build`）；移动端 `npm run build:mobile`。
- **测试**：后端 `ctest --test-dir backend/build`（约 87 个目标，覆盖财务精度、账本重建、领域规则、AI 工具/策略/运行时、MCP、调度、安全回归等）；前端 `npm test`（Vitest）。
- **部署**：Docker Compose 双容器（后端 :8080 + Nginx :80），默认 SQLite（`minefolio-data` 卷），可选 `COMPOSE_PROFILES=postgres` 启用 PostgreSQL。配置见 `backend/config/minefolio.yaml`、`.env.example` 与 `README.md`。

---

## 7. 二次开发指南

| 任务 | 入口与要点 |
|------|-----------|
| 新增 HTTP API | 在 `application/<domain>/` 增 usecase/command/dto → `domain/<domain>/` 增规则/仓储契约 → `infrastructure/repositories/` 实现 → `interfaces/http/controllers/<name>_controller.c` 用 `csilk_app_*_ext` 注册 → 在 `main.c` 调用 `register_<name>_routes` |
| 新增 AI 工具 | 在 `services/ai/tools/` 定义 `ai_tool_t` 并调用 `ai_tool_register`；声明只读/变更、是否 `internal_only`；在对应 `*_register_all()` 中挂载 |
| 新增工作流 | 在 `services/ai/workflows/` 构造 `ai_workflow_graph_t`，并在 `services/ai/workflow/engine.c` 的 `ensure_init_builtin_workflows()` 注册 |
| 新增数据库迁移 | 在 `sql/migrations/sqlite/` 与 `postgres/` 各新增同名 `V<NNN>__*.sql`，保持双方言语义一致；无需手工改旧库 |
| 新增领域 | 按 `application/<domain>/{usecases,commands,dtos}`、`domain/<domain>/{entity,repository,rules}`、`infrastructure/repositories/*_repo_impl.c` 骨架落地，再接 controller |

---

## 8. 相关文档索引

- `docs/architecture.md`：工程级完整架构说明（分层、数据模型、关键数据流、部署拓扑、安全边界）。
- `docs/ai-runtime-architecture.md`：AI 运行时子系统细节。
- `docs/financial-core-architecture.md`、`docs/ledger-engine-ownership-audit.md`：财务核心与账本所有权。
- `docs/codebase-analysis-*.md`、`docs/security-audit*.md`：阶段性代码与安全审计。
- `README.md`：快速启动、部署与特性总览；`AGENTS.md`：开发约定；`CHANGELOG.md`：版本历史。
- `docs/superpowers/{specs,plans}/`：按时间记录的单次功能设计/实施历史（档案，非现状描述）。
