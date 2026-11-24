# Minefolio

[![Release: v1.4.0](https://img.shields.io/badge/Release-v1.4.0-success.svg)](https://github.com/quintin-lee/Minefolio/releases/tag/v1.4.0)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/C-23-00599C?logo=c)](https://en.cppreference.com/w/c/23)
[![Vue 3](https://img.shields.io/badge/Vue-3.4+-4FC08D?logo=vue.js)](https://vuejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.5-3178C6?logo=typescript)](https://www.typescriptlang.org/)
[![Vite](https://img.shields.io/badge/Vite-5.4-646CFF?logo=vite)](https://vitejs.dev/)
[![SQLite](https://img.shields.io/badge/SQLite-WAL-003B57?logo=sqlite)](https://www.sqlite.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-16+-4169E1?logo=postgresql)](https://www.postgresql.org/)

Minefolio 是一款极度轻量、安全、专业的**开源全资产管理与投资追踪系统**。
后端采用现代化 **C23 标准** 及高性能 **csilk 框架** 开发，常驻内存仅约 15MB，单节点吞吐达数万 QPS；前端基于 **Vue 3 + TypeScript** 与 **Element Plus** 构建，支持桌面端与移动端（Capacitor + SQLite WASM 纯离线运行）。

---

## 🌟 核心特性与亮点

* 🧮 **高精度金融核心与定点数引擎 (Financial Core)**：基于 64 位有符号整型定点数与 128 位 `decimal_t` 扩展精度（资产/行情用例层聚合全程 128 位，仅在 JSON DTO 边界回落 double）与标准舍入算法，彻底消除浮点数计算误差，支持高精度货币 (Money)、持仓数量 (Quantity)、成交价 (Price)、汇率 (Rate)、百分比 (Percentage) 与盈亏 (PnL) 严谨核算。
* 🏛️ **统一账本状态引擎 (Ledger Engine)**：确立「Transaction 是唯一金融事实」准则，资产持仓、现金余额、成本基础、已实现/浮动盈亏全由账本引擎统一计算与更新，并提供基于时间序列事实全量重建 (`rebuild`) 的自愈能力；V020 引入账本检查点快照 (`ledger_snapshots`)，重建从最近有效检查点增量推进而非全量回放，回溯改账自动级联失效下游快照；交易落库时同步固化 Point-in-Time 历史汇率 (`transactions.fx_rate_to_cny`)，历史估值不再随其后汇率变动漂移。
* 🤖 **统一 AI Runtime 与企业级五级风控引擎 (Unified AI Runtime & Risk Engine)**：建立原生 C 统一 AI 运行时架构，将 Controller 与 Workflow 彻底与 LLM 调用解耦，纳管会话 (Session)、统一上下文容器 (Context)、模型适配 (Model)、工具执行 (Tool)、工作流编排 (Workflow)、安全策略 (Policy)、全链路追踪 (Trace) 与滑动窗口记忆 (Memory)；全面支持硬预算与约束（迭代上限、超时、Token/Tool/Cost 预算与主动取消）及结构化错误分类，实行五级风险评估与带 Nonce 防重放的双重确认令牌机制（V019 将已消费 Nonce 持久化至 `ai_consumed_nonces` 表，重放防护跨进程重启生效）。
* 🔑 **统一 Secret Provider 与零硬编码安全门禁**：建立统一敏感配置检索机制，原生兼容环境变量、Docker/K8s Secret 文件挂载与外部密钥管理器 (Vault/AWS/K8s)；在生产启动期实施硬编码/弱口令主动熔断。
* 🗄️ **企业级数据库迁移系统 (Database Migration System)**：下沉至 csilk 框架的原生 C 版本化迁移引擎（`<csilk/drivers/db/migration.h>`），支持 SQLite / PostgreSQL 双方言（`V001`~`V033`，覆盖 **37 张核心业务与运维表**）；具备版本严格发现排序、SHA-256 CRLF 规范化哈希防篡改、基于行锁的分布式迁移互斥锁，并在 PostgreSQL 驱动层支持原生 Prepared Statement（`?` 自动转 `$N` 与参数绑定），实现生产环境存量数据库平滑升级（Auto-Baseline），绝不要求删除已有数据库重新初始化。
* 💼 **全资产全场景覆盖**：支持现金、银行储蓄、股票、基金、债券、加密货币、房产、贷款、信用卡等资产与负债统一建档，支持负债方向自动正负号翻转。
* 🗂️ **账户维度资产管理与细粒度 RBAC**：以独立「账户」实体（银行卡/平台/券商/基金平台/现金钱包）归集各类资产，账户卡片分组呈现小计与资产数；支持账户归档（软删除）、手动排序与 `V031` 账户级/字段级 RBAC 权限（隐藏账户、余额/卡号掩码、禁止写入、汇总/报表/AI 摘除隔离）；全链路资产选择入口均以「账户 · 资产」前缀区分同名资产。
* 📈 **专业资管级投资模型与持仓分析**：加权均价 / FIFO 持仓批次（`transaction_lots`，V021）与高精度量价存储（V032）、分红摊薄计价、卖出部分成本按比例核减、手续费联动与基于 `parent_tx_id` 的级联回滚；支持持仓总览（`position-overview`）、HHI 集中度评估、当日盈亏与单标详情页。
* 🌐 **多币种实时外汇引擎 (FX Engine)**：支持全币种资产管理，内置 Yahoo Finance 实时汇率同步与手动汇率锁定标记（V026），支持基准折算净资产实时呈现。
* 📊 **外汇损益双因子归因分析**：在报表中心将境外资产收益严格拆解为「**资产标的自身价格涨跌**」与「**纯外汇汇率波动汇兑损益 (FX PnL)**」。
* 👥 **多账本空间与多用户协作**：支持个人、家庭、团队多账本独立隔离与邀请码加入，实行 `Owner` / `Editor` / `Viewer` 三角色空间鉴权与账户级细分权限；移动端离线 SQLite 同样按 `ledger_id` 隔离并作用域同步。
* 🎯 **智能定投计划 (DCA) 与现金流日历**：支持周期性定投计划排程与一键确认买入；支持分红/利息/租金排程推演与 30/90 天现金流预测日历。
* 📸 **智能票据 OCR 识别与录单**：桌面端与移动端支持调用 OpenAI/DeepSeek 视觉多模态大模型解析发票与小票，内置纯离线启发式规则兜底，一键拍照自动填单并智能匹配分类。
* ⚡ **多源行情定时同步引擎与调度中心 (Scheduler Center)**：内置纯 C 5 段式 Cron 解析器、NAS 磁盘休眠友好的动态休眠工作池与内置系统定时任务（行情同步、备份、Market Pulse 早报、微信/Webhook 推送），支持运行日志审计与即时手动触发。
* 🤖 **AI 财务助理、Agent Skills 与 MCP 工具箱**：多轮财务上下文对话、**26 个内置工具** + **13 个多步分析工作流**（含条件分支与 Saga 补偿）、动态接入远程 MCP Server 与对外 `/mcp` JSON-RPC 服务端；支持 Agent Skills 技能库（V022，Markdown 导入导出 + `list_skills`/`use_skill` 工具）、Market Pulse 每日财经早报两阶段摘要（V024/V028）及异步后台任务队列（`async_tasks`，V033）。
* 💬 **IM 机器人只读查账网关 (`gateway/`)**：独立无依赖 Node.js 网关（支持 QQ OneBot v11 / NapCat 与 WeChat），通过 `im_bind` 专属令牌接入 `/api/im/*`（支持自助账号绑码 `/api/im/:platform/bind-auth`，2FA 账号受保护），强制只读工具白名单与抢占防护。
* 🧠 **AI 深度思考推理模式 (Thinking / Reasoning Mode)**：原生适配 DeepSeek-R1 / OpenAI o-series / Claude 等前沿推理模型，流式捕获思维链并持久化至数据库 (`ai_messages.thinking_content`, V015)；对话内支持财务实体悬停微卡片、`@` 账户提及、智能追问建议与多模态粘贴/OCR 上传。
* 🖥️ **现代 AI 工作台与三栏式分析面板 (Analysis Panel & Flex Layout)**：提供 3 栏弹性工作台（会话侧栏、对话流、可拖拽/折叠右侧多维分析面板）；独创 Chat 专属 Full-Bleed 贴边与 Flex 纵向底栏解耦机制，支持全局金额一键掩码切换（`AmountMaskToggle`）。
* 🔐 **银行级安全与灾备运维工具**：全链路 RSA-OAEP 前端口令加密传输、JWT 版本号即时吊销、TOTP 2FA 动态双因素认证、GitHub / 通用 OIDC 单点登录；内置 SQLite 在线热备份与下载管理、优雅退出 WAL Truncate Checkpoint，以及独立运维 CLI 工具 `reset-password`。
* 📱 **双端同构与移动端纯离线**：基于 Capacitor 与 base64 嵌入式 `sql.js` WASM SQLite（支持 WebCrypto 非导出密钥加密本地库与自建服务端地址动态配置/延迟探测）；移动端内置 AI 对话 Tab，界面统一为简体中文（`zh-CN`）。
* 🔬 **框架级可观测性管理后台**：内置 `/csilk-admin` 性能仪表盘，支持实时 RPS 与 HTTP 状态监控、工作流 Universal DAG 动态拓扑图可视化，以及 100Hz CPU 堆栈火焰图 Profiler。

---

## 🛠️ 技术栈

| 层次 | 技术选型 | 说明 |
| :--- | :--- | :--- |
| **后端语言 & 标准** | C23 (GCC 14 / Clang 18) | 原生编译，极低资源占用（~2.8MB 二进制，~15MB 内存） |
| **Web 框架** | [csilk](https://github.com/quintin-lee/csilk) v1.2.0 | 高性能现代 C 异步 HTTP/HTTP2 框架 |
| **数据库** | SQLite 3.35+ (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移；RETURNING 语法要求 SQLite ≥ 3.35 |
| **JSON 解析** | yyjson | 超高性能 C 语言 JSON 序列化/反序列化库 |
| **前端架构** | Vue 3 + TypeScript + Vite | 严格模式 `vue-tsc -b` 0 错误编译 |
| **UI 组件库** | Element Plus + Iconify (Phosphor Icons) | Glassmorphism 科技感暗色/渐变主题 |
| **数据可视化** | ECharts 5 + Mermaid.js | 净资产走势、资产占比、外汇走势、工作流 DAG 拓扑图 |
| **移动端离线存储** | Capacitor 6 + sql.js (WASM) | 离线纯前端 SQLite 数据库 |
| **安全机制** | RSA-OAEP + SHA-256 + HMAC-SHA256 JWT + TOTP | 密码防截获重放、2FA 硬件防撞 |

---

## 🚀 快速启动与部署指南

### 方式 1：Docker Compose 容器化部署（推荐）

Minefolio 采用生产级双容器解耦架构：
* **`minefolio` 后端容器**：基于 C23 + csilk 的高性能原生服务，负责 API 业务逻辑、账本引擎与 SSE 流式输出（监听 :8080）。
* **`minefolio-nginx` 前端网关容器**：基于 Nginx Alpine 托管 Vue 3 SPA 静态资源，并对 API 请求进行弹性反向代理（监听 :80 端口）。

#### 1. 克隆代码仓库并配置环境
```bash
git clone https://github.com/quintin-lee/Minefolio.git
cd Minefolio

# 复制环境变量模板
cp .env.example .env

# 编辑 .env 文件，务必配置强随机密钥（未配置或使用默认弱密钥将在启动时被安全门禁拦截）
# 快速生成强密钥命令: openssl rand -hex 32
vim .env
```

#### 2. 一键启动服务
```bash
# 启动多容器服务（默认自动拉取 GitHub Container Registry 官方镜像）
docker compose up -d

# 查看容器运行与健康检查状态（稍候片刻两个容器均应为 healthy）
docker compose ps
```
> 说明：容器默认时区为 `Asia/Shanghai`。若需调整，可在 `docker-compose.yml` 中修改 `TZ` 环境变量或变更 `/etc/localtime` 挂载。

启动成功后，在浏览器访问 `http://localhost` 即可进入初始化向导创建管理员账户。

#### 3. 部署技巧与进阶配置
* **锁定特定版本部署（如 v1.4.0）**：
  可在 `docker-compose.yml` 中修改镜像标签为 `:v1.4.0`，或执行拉取与重打标签：
  ```bash
  docker pull ghcr.io/quintin-lee/minefolio:v1.4.0
  docker pull ghcr.io/quintin-lee/minefolio-frontend:v1.4.0
  docker tag ghcr.io/quintin-lee/minefolio:v1.4.0 ghcr.io/quintin-lee/minefolio:latest
  docker tag ghcr.io/quintin-lee/minefolio-frontend:v1.4.0 ghcr.io/quintin-lee/minefolio-frontend:latest
  ```
* **从本地源码构建镜像**（二次开发或自定义修改后）：
  ```bash
  # 构建后端原生镜像
  docker build -t ghcr.io/quintin-lee/minefolio:latest .
  # 构建前端 Nginx 镜像
  docker build -f Dockerfile.frontend -t ghcr.io/quintin-lee/minefolio-frontend:latest .
  # 启动更新后的容器
  docker compose up -d
  ```
  > 注意：`docker-compose.yml` 未声明 `build:`，`docker compose up -d --build` 不会构建任何镜像——改完代码必须先执行上面两条 `docker build`，再 `up -d`。
* **随部署一起启动内置 PostgreSQL 容器（可选）**：
  默认部署使用 SQLite（持久化于 `minefolio-data` 卷），无需额外服务。
  如需让 `docker compose` 同时拉起一个 `db-postgres` 容器，在 `.env` 中启用 postgres profile：
  ```env
  COMPOSE_PROFILES=postgres
  MINEFOLIO_DB_DRIVER=postgres
  MINEFOLIO_DB_PASSWORD=replace-with-db-password
  ```
  然后 `docker compose up -d`（`.env` 里的 `COMPOSE_PROFILES` 会被自动读取；也可在命令行加 `--profile postgres`）。
  未启用时（`COMPOSE_PROFILES` 留空）不会创建 `db-postgres` 容器，只部署 SQLite。

  > 说明：`Dockerfile` 已默认安装 libpq 并在构建期编入 csilk 的 PostgreSQL 驱动，`ghcr` 官方镜像开箱即用，无需额外 `--build-arg`。若部署的是不含 libpq 的旧版镜像，请重新执行上面的 `docker build` 构建后再启动。
  > 内置 `db-postgres` 容器使用 `minefolio-pgdata` 卷持久化；`docker/db-wait.sh` 会在后端启动前等待数据库端口就绪。
* **使用外部 PostgreSQL 生产数据库**：
  不部署内置容器时（保持 `COMPOSE_PROFILES` 留空），在 `.env` 中指定外部连接串即可：
  ```env
  MINEFOLIO_DB_DRIVER=postgres
  MINEFOLIO_DB_DSN=host=your-pg-host port=5432 user=minefolio password=your-db-pass dbname=minefolio sslmode=disable
  ```
  同样需要镜像带 libpq（`Dockerfile` 默认已包含）。原生 C 数据库迁移引擎会在启动时自动完成版本化迁移（`V001`~`V033`）。

### 方式 2：本地源码编译与开发

#### 后端构建：
```bash
cd backend
# 必须使用 Unix Makefiles 生成器（避免 Ninja 依赖陈旧问题）
cmake -B build -G "Unix Makefiles"
cmake --build build --parallel

# 启动后端服务（默认监听 :8080）
MINEFOLIO_JWT_SECRET="your_development_secret_key_12345" ./build/minefolio
```

#### 前端构建：
```bash
cd frontend
npm install

# 启动桌面端开发服务（端口 5173，自动代理 /api 至 :8080）
npm run dev

# 启动移动端开发预览（端口 5174）
npm run dev:mobile

# 前端生产打包与严格类型检查
npm run build         # 桌面端
npm run build:mobile  # 移动端
```

---

## 📂 项目工程结构

```
Minefolio/
├── backend/                      # C23 后端服务
│   ├── CMakeLists.txt            # 构建配置（C23、csilk v1.2.0、yyjson、81 CTest targets）
│   ├── tools/                    # 独立运维 CLI 工具（reset-password）
│   ├── sql/                      # 数据库迁移与初始化脚本
│   │   ├── migrations/           # 版本化迁移脚本（V001~V033）
│   │   │   ├── sqlite/           # SQLite 方言版本脚本 (33 个迁移)
│   │   │   └── postgres/         # PostgreSQL 方言版本脚本 (33 个迁移)
│   │   ├── migration.sql         # 37 张核心数据表全量基准（测试与初始化）
│   │   └── migration_postgres.sql
│   ├── src/
│   │   ├── main.c                # 服务主入口、中间件装配、27 组路由注册与静态托管
│   │   ├── interfaces/           # 表现层：HTTP Controllers（27 个控制器，委托到 Application）
│   │   │   └── http/controllers/ # account/admin/ai/ai_mcp/ai_news/ai_trace/asset/auth/cashflow/category/daily_expense/dca/file/im_bot/import_export/import_rule/ledger/market/mcp_server/receipt/report/scheduler/skill/tag/task/transaction/transfer
│   │   ├── application/          # 应用层：用例编排（21 个业务域 + async_task_manager）
│   │   │   └── <domain>/         # account, ai, asset, auth, cashflow, category, daily_expense, dca, file, import_export, ledger, market, mcp, portfolio, report, scheduler, skill, tag, task, transaction, transfer
│   │   ├── domain/               # 领域层：纯业务实体、规则与仓储契约（21 个业务域）
│   │   ├── infrastructure/       # 基础设施层
│   │   │   ├── database/         # 数据库抽象、连接、SQLite 热备份 (db_backup.c) 与原生 SQLite/Postgres 适配器
│   │   │   └── repositories/     # 27 个仓储实现（含 account_permission_repo_impl, async_task_repo_impl, ledger_snapshot_repo_impl 等，零 legacy 残留）
│   │   ├── core/                 # 金融核心与基础引擎
│   │   │   ├── financial/        # 128位定点数（money, decimal, quantity, price, rate, currency, percentage）
│   │   │   └── ledger/           # 事件溯源账本引擎（ledger_apply_tx/reverse/rebuild + checkpoint）
│   │   ├── services/             # 业务服务层
│   │   │   ├── ai/               # 统一 AI Runtime（runtime, model, workflow/workflows, tools [26 内置 + MCP], policy, trace, memory）
│   │   │   ├── market/           # 行情引擎、外汇服务、行情调度
│   │   │   ├── notification/     # 微信/Webhook 推送服务
│   │   │   └── scheduler/        # Cron 解析器与后台调度中心引擎
│   │   ├── common/               # 公共组件（db, jwt, balance, response, ocr, tx_types, rbac；CSV 与 TOTP 2FA 已下沉至 csilk）
│   │   ├── config/               # 密钥对管理、数据库配置、统一 Secret Provider (secret.h/.c)
│   │   └── middlewares/          # JWT 鉴权、CORS、CSRF、限流与安全头中间件
│   └── tests/                    # 单元测试与集成测试矩阵
│       ├── unit/                 # 81 项 CTest 单元测试（19 项纯金融/领域独立套件 + 62 项联动核心套件）
│       ├── run_isolated.sh       # cgroups v2 内存隔离构建与测试脚本
│       ├── db_helper.sh          # 跨引擎统一数据库测试桩（端口冲突自愈、环境检测、双引擎重置）
│       └── test_*.sh             # 21 个集成与辅助脚本（其中 13 个支持 SQLite / PostgreSQL 双引擎）
│                                 # 含 test_link.sh (244 断言), test_ledgers.sh, test_2fa.sh, test_dca_cashflow.sh,
│                                 # test_ai_trace.sh, test_ai_tool_call.sh, test_ai_truncate.sh, test_ai_loop_hardening.sh,
│                                 # test_ai_news_isolation.sh, test_market_sync.sh, test_fx_oauth.sh, test_mcp.sh,
│                                 # test_mcp_server.sh, test_report_isolation.sh, test_scheduler.sh,
│                                 # test_im_bot.sh, test_im_bind_auth.sh, test_gateway.sh 等
├── gateway/                      # 独立 IM 机器人网关（Node >= 18，零外部依赖）
│   ├── index.js                  # 消息路由、/bind 与自助账密绑定（im_bindings.json 持久化）
│   ├── cli.js                    # 本地 CLI 闭环调试工具
│   └── adapters/                 # OneBot v11 (QQ/NapCat) 与 WeChaty 适配器（11 个适配器单元测试）
├── frontend/                     # Vue 3 + TypeScript 现代化前端
│   ├── src/
│   │   ├── api/                  # 26 个强类型 API 模块（桌面端与移动端共享）
│   │   ├── stores/               # 8 个 Pinia 状态管理（auth, category, chat, display-settings, ledger, market, sync, theme）
│   │   ├── views/                # 24 个桌面端页面（Dashboard, Assets, AssetDetail, Holdings, Plans, Reports, Chat, Scheduler, Skills, Mcp, NewsPulse, Settings...）
│   │   ├── views-mobile/         # 11 个移动端页面（含内嵌 AI 对话与服务端动态配置）
│   │   ├── components/           # 45 个组件（ECharts 图表、DAG 可视化、扫描弹窗、全局掩码开关、移动端组件）
│   │   ├── composables/          # 组合式函数（useServerUrl, useAsyncTaskProgress, useFinancialEntities, useI18n [zh-CN], chat/*）
│   │   ├── utils/                # 通用工具库（http, server-url, crypto, format, offline-http, idb-key, amount-mask）
│   │   ├── db/                   # sql.js WASM 离线 SQLite 驱动（10 张本地镜像与同步表）
│   │   ├── router/               # 桌面与移动端独立双路由
│   │   └── types/                # 全局 TypeScript 接口模型定义
│   ├── tests/                    # 53 个 Vitest 前端单元测试套件
│   └── vite.config.ts            # Vite 桌面与移动端多模式打包配置
├── scripts/                      # 快捷开发与构建脚本
│   ├── dev.sh                    # 一键启动前后端双开发服务
│   └── build.sh                  # 一键生成 Release 生产部署包
├── Dockerfile                    # 多阶段自动化构建镜像（含 minefolio 与 reset-password）
├── Dockerfile.frontend           # 前端 Nginx 多阶段镜像
└── docker-compose.yml            # 生产级多容器编排（支持 postgres 与 im profiles）
```

---

## 📑 核心 API 端点清单

### 1. 认证、安全与单点登录 (Auth & SSO)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `POST` | `/api/auth/register` | 公开 | 用户注册（支持 RSA 前端公钥加密密码） |
| `POST` | `/api/auth/login` | 公开 | 用户登录（支持 2FA 拦截与临时验证凭证） |
| `GET` | `/api/auth/public-key` | 公开 | 获取用于前端加密的 RSA-OAEP 公钥 JWK |
| `GET` | `/api/auth/me` | JWT | 获取当前登录用户的个人信息 |
| `PUT` | `/api/auth/password` | JWT | 修改用户登录密码 |
| `POST` | `/api/auth/2fa/setup` | JWT | 生成 TOTP 双因素认证密钥与绑定二维码 |
| `POST` | `/api/auth/2fa/enable` | JWT | 校验动态码并正式启用 2FA |
| `POST` | `/api/auth/2fa/disable` | JWT | 关闭 2FA 双因素保护 |
| `POST` | `/api/auth/2fa/verify-login` | 公开 | 提交 2FA 动态码完成登录 |
| `GET` | `/api/auth/oauth/providers` | 公开 | 获取启用的第三方单点登录源（GitHub / OIDC） |
| `POST` | `/api/auth/oauth/callback` | 公开 | 第三方授权回调与自动 Provision 用户登录 |

### 2. 多账本空间与 RBAC 协作 (Ledgers & RBAC)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET` | `/api/ledgers` | JWT | 获取当前用户加入的所有账本空间 |
| `POST` | `/api/ledgers` | JWT | 创建新的独立账本（支持指定币种与图标） |
| `GET/PUT/DELETE` | `/api/ledgers/:id` | JWT | 获取、更新或解散指定的账本空间 |
| `GET/POST` | `/api/ledgers/:id/members`| JWT | 获取成员列表或添加成员 |
| `POST` | `/api/ledgers/:id/invite-code`| JWT | 生成加入账本的有效期邀请码 |
| `POST` | `/api/ledgers/join` | JWT | 凭邀请码加入目标账本 |
| `GET/PUT/DELETE` | `/api/ledgers/:ledger_id/accounts/:id/permissions` | JWT | 管理共享账本内账户级可见性与字段脱敏权限 (`none`/mask) |

### 3. 资产、负债与交易 (Assets & Transactions)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET/POST` | `/api/accounts` | JWT | 账户列表（含 asset_count/total_value 聚合，`?include_archived=1` 含归档）与新建账户 |
| `PUT/DELETE` | `/api/accounts/:id` | JWT | 编辑账户字段与归档/恢复；删除有资产或交易的账户返回 1004 |
| `GET/POST` | `/api/assets` | JWT | 资产列表（分页/过滤，JOIN 账户名与机构，支持 `?account_id=`）与新建资产 |
| `GET/PUT/DELETE` | `/api/assets/:id` | JWT | 资产详情（含 account_name）、编辑资产净值与改挂账户、删除资产 |
| `POST` | `/api/assets/:id/rebuild` | JWT | 账本引擎：重放单标的事实交易，全量重建持仓量、成本与市值 |
| `POST` | `/api/assets/rebuild` | JWT | 账本引擎：全量重建用户所有投资资产的持仓与成本状态 |
| `GET/POST` | `/api/transactions` | JWT | 交易记录（买入/卖出/存取/分红，支持 fee 联动） |
| `PUT/DELETE` | `/api/transactions/:id` | JWT | 编辑或删除交易（带 fee 子行级联回滚） |
| `GET/POST` | `/api/daily-expenses` | JWT | 日常记账收支列表与创建收支 |
| `POST` | `/api/transfers` | JWT | 账户间跨资产转账（带 from_tx_id / to_tx_id 显式关联与配对流水） |

### 4. 计划、定投与现金流日历 (Plans & Cashflow)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET/POST` | `/api/dca/plans` | JWT | 获取定投计划列表与创建定投策略 |
| `GET/PUT/DELETE` | `/api/dca/plans/:id` | JWT | 查看、编辑或删除指定定投计划 |
| `GET` | `/api/dca/executions/pending` | JWT | 查询当前所有待执行的定投买入任务 |
| `POST` | `/api/dca/executions/:id/confirm` | JWT | 一键确认并自动执行定投扣款与买入 |
| `POST` | `/api/dca/executions/:id/skip` | JWT | 跳过当期定投执行 |
| `GET/POST` | `/api/cashflow/schedules` | JWT | 获取与创建定期现金流收支排程 |
| `GET` | `/api/cashflow/calendar` | JWT | 查询指定年月的现金流预测日历与实到核销状态 |
| `POST` | `/api/cashflow/confirm` | JWT | 确认日历中的现金流到账并入账对应资产 |

### 5. 行情、外汇与报表分析 (Market & Reports)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET` | `/api/market/search` | JWT | 检索股票/基金/加密货币标的代码 |
| `GET` | `/api/market/quote` | JWT | 拉取单一标的当前最新实时行情 |
| `GET` | `/api/market/indices` | JWT | 获取全球大盘主要指数实时报价与日内分时 |
| `POST` | `/api/market/sync` | JWT | 批量刷新当前用户持仓资产的市场价格与净值 |
| `GET` | `/api/market/exchange-rates` | JWT | 获取多币种实时外汇对汇率表 |
| `POST` | `/api/market/exchange-rates` | JWT | 手动设置与更新特定外汇汇率 |
| `GET` | `/api/market/fx-history` | JWT | 获取指定外币兑基准币的历史走势快照 |
| `GET` | `/api/summary` | JWT | 仪表盘资产总额、负债、净资产总览 |
| `GET` | `/api/reports/multi-currency-summary` | JWT | 多币种资产构成与指定基准币种一键折算报表 |
| `GET` | `/api/reports/fx-pnl` | JWT | 境外外币资产「价格盈亏 vs 汇率损益」剥离归因报表 |
| `GET` | `/api/reports/holdings` | JWT | 投资持仓明细（持仓量、摊薄成本、浮动盈亏） |
| `GET` | `/api/reports/position-overview` | JWT | 持仓总览（区间收益、当日盈亏、HHI 集中度等增强指标） |
| `GET` | `/api/reports/transaction/performance` | JWT | 投资交易历史绩效分析（实现收益、胜率、分红） |

### 6. 智能识别、AI 助理、MCP、调度、IM 与运维 (AI, Scheduler, IM & Ops)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `POST` | `/api/receipts/scan` | JWT | 发票/收据图片 OCR 多模态智能识别与自动分类填单 |
| `GET/POST` | `/api/import-rules` | JWT | 查看与配置商户/关键词智能记账规则 |
| `POST` | `/api/import-rules/reset-defaults` | JWT | 一键重置恢复系统推荐的智能分类规则库 |
| `GET` | `/api/ai/workflows` | JWT | 获取 13 个内置智能财务分析工作流模板与步骤说明 |
| `POST` | `/api/ai/workflows/run` | JWT | 启动执行指定智能工作流（SSE 流式实时推送执行事件） |
| `POST` | `/api/ai/chat` | JWT | 与 AI 财务分析助理多轮上下文对话（支持 26 个内置工具 + MCP 工具调用） |
| `POST` | `/api/ai/actions/confirm` / `confirm-batch` | JWT | 二次确认并原子执行高风险/批量拟记账提案（HMAC + Nonce 防重放） |
| `GET/POST` | `/api/ai/skills*` | JWT | Agent Skills 技能管理（列表、导入/导出 Markdown、启用/测试） |
| `GET/POST` | `/api/ai/news*` | JWT | Market Pulse 每日财经早报两阶段摘要列表、详情与手动触发 |
| `GET` | `/api/ai/mcp/*` / `POST /mcp` | JWT | 管理外部 MCP Server/脚本，以及对外暴露 JSON-RPC `/mcp` 服务端点 |
| `GET` | `/api/ai/traces*` | JWT | 查询 AI 会话的全链路耗时、Token 消耗与调用 Spans |
| `GET/POST/PUT/DELETE` | `/api/scheduler/*` | JWT | 定时任务管理、内置任务启停/立即触发、运行日志与微信推送通道配置 |
| `GET/POST` | `/api/im/*` | JWT / `im_bind` | IM 机器人绑定列表、一次性绑定码、`/im/:platform/bind-auth` 自助绑定与只读查账 |
| `GET/DELETE` | `/api/tasks*` | JWT | 异步后台任务状态轮询与主动取消 |
| `GET/POST/DELETE` | `/api/system/backup/*` | JWT | SQLite 在线热备份创建、列表状态、下载与清理 |
| `GET` | `/csilk-admin/` | 公开 | 框架级可观测性管理面板（RPS、DAG 拓扑、火焰图） |

---

## ⚙️ 环境变量配置参考

| 环境变量名 | 类型 | 必填 | 默认值 | 功能说明 |
| :--- | :---: | :---: | :---: | :--- |
| `MINEFOLIO_JWT_SECRET` | 字符串 | **是** | - | JWT 签名密钥（生产环境未设置将禁止启动） |
| `MINEFOLIO_PORT` | 整数 | 否 | `8080` | 后端服务监听端口 |
| `MINEFOLIO_DB_DRIVER` | 字符串 | 否 | `sqlite` | 数据库类型：`sqlite` 或 `postgres` |
| `MINEFOLIO_DB_DSN` | 字符串 | 否 | `./data/minefolio.db` | 数据库连接字符串（SQLite 文件路径或 PG 连接串） |
| `MINEFOLIO_ENABLE_CSRF` | 布尔 | 否 | `0` | 是否开启严格的双重 Cookie CSRF 拦截 |
| `MINEFOLIO_OAUTH_GITHUB_CLIENT_ID` | 字符串 | 否 | - | GitHub OAuth 登录 Client ID |
| `MINEFOLIO_OAUTH_GITHUB_CLIENT_SECRET` | 字符串 | 否 | - | GitHub OAuth 登录 Client Secret |
| `MINEFOLIO_OAUTH_OIDC_CLIENT_ID` | 字符串 | 否 | - | 通用 OIDC 单点登录 Client ID |
| `MINEFOLIO_OAUTH_OIDC_AUTH_URL` | 字符串 | 否 | - | 通用 OIDC 授权端点 URL |
| `MINEFOLIO_AI_API_KEY` | 字符串 | 否 | - | 模型 API Key（为空时回退读取 `config/ai.json`） |
| `MINEFOLIO_TRACE_RETENTION_ROWS` | 整数 | 否 | `0` | 每个用户保留的最新 `ai_traces` 行数上限（`0` 为不清理） |
| `MINEFOLIO_MCP_CACHE_TTL` | 整数 | 否 | `1800` | MCP stdio 子进程池空闲回收 TTL（秒） |

---

## 🧪 自动化测试体系

Minefolio 拥有完整的双层测试矩阵（单元测试 + 端到端集成回归）：

### 1. CTest 单元测试矩阵（81 大测试套件，高频毫秒级断言）
```bash
./backend/tests/run_isolated.sh -m 4G -j 4 unit build
# 或在 backend/build 中运行：
ctest --output-on-failure
# 核心测试域（81 targets = 62 联动 core 套件 + 19 纯金融/领域独立套件）：
# - 金融核心定点数：test_currency, test_decimal, test_money, test_quantity, test_price, test_rate, test_pnl, test_fx, test_fx_semantics
# - 统一账本与批次成本：test_ledger_math, test_ledger_engine, test_ledger_rebuild_checkpoint, test_transaction_lots, test_cost_basis_fifo
# - 领域业务规则与用例：test_domain_{transaction,asset,auth,ai,mcp,skill,account,tag,category,ledger,dca,portfolio,cashflow,market,cost_basis,pnl,performance,position,multi_currency}
#                     test_domain_repositories, test_account_usecases, test_account_rbac, test_tag_usecases, test_category_usecases, test_report_services, test_portfolio_pnl_alignment
# - 统一 AI Runtime 与风控：test_ai_tools, test_ai_policy, test_mcp_pool, test_policy_mcp_risk, test_ai_runtime, test_ai_summary, test_ai_mask,
#                          test_nonce_persistence, test_ai_token_compact, test_ai_batch_confirmation, test_ai_action_confirm,
#                          test_ai_prompt_injection_defense, test_ai_config_provider, test_ai_news_privacy, test_skill_repository
# - MCP / 工作流 / 异步调度 / 灾备：test_mcp_client_io, test_mcp_stderr_capture, test_mcp_child_sanitation, test_mcp_unauth_protocol,
#                                   test_ai_parallel_tools, test_workflow_step_error, test_workflow_branching, test_summary_worker_pool,
#                                   test_cron_parser, test_scheduler_migration, test_scheduler_repo, test_scheduler_engine, test_scheduler_idle_sleep, test_wechat_push_service,
#                                   test_wal_checkpoint, test_db_backup, test_async_task_manager
# - 精度、事务、安全与数据库：test_tx_scope_nesting, test_financial_precision_edge, test_file_parser_mkstemps,
#                            test_auth_2fa_lifecycle, test_oauth_security, test_secret_provider, test_database_repository, test_migration_engine
```

### 2. 端到端集成测试矩阵（21 个测试脚本，13 个支持 SQLite / PostgreSQL 双引擎）
Minefolio 端到端集成测试通过 `backend/tests/db_helper.sh` 统一封装 **SQLite (默认)** 与 **PostgreSQL** 双数据库引擎生命周期与 Schema 重置：
```bash
# 运行核心闭环测试（SQLite 默认，244 断言）：
./backend/tests/test_link.sh

# 专题回归测试（SQLite / PostgreSQL 双引擎均支持）：
./backend/tests/test_ledgers.sh && ./backend/tests/test_2fa.sh && \
./backend/tests/test_dca_cashflow.sh && ./backend/tests/test_ai_trace.sh && \
./backend/tests/test_ai_tool_call.sh && ./backend/tests/test_market_sync.sh && \
./backend/tests/test_fx_oauth.sh && ./backend/tests/test_scheduler.sh && \
./backend/tests/test_im_bot.sh && ./backend/tests/test_im_bind_auth.sh && \
./backend/tests/test_gateway.sh

# 另有专项脚本（SQLite）：test_ai_truncate.sh, test_ai_loop_hardening.sh, test_ai_news_isolation.sh,
# test_mcp.sh, test_mcp_server.sh, test_report_isolation.sh 等。

# 指定 PostgreSQL 运行双引擎集成测试：
export MINEFOLIO_DB_DRIVER=postgres
export MINEFOLIO_DB_DSN="host=localhost port=5432 user=postgres password=postgres_password dbname=minefolio_test sslmode=disable"
export PGHOST=localhost PGPORT=5432 PGUSER=postgres PGPASSWORD=postgres_password PGDATABASE=minefolio_test
./backend/tests/test_link.sh

# CI 自动化流水线 (.github/workflows/ci.yml)：
# 包含并行的 backend (SQLite: CTest + 18 个专项脚本)、backend-postgres (PG 16: CTest + 12 个双引擎脚本)、
# frontend (build + Vitest) 与 gateway (11 个 IM 适配器测试) 任务。
```

### 3. 前端与 IM 网关单元测试矩阵（53 个前端套件 + 11 个网关适配器用例）
```bash
cd frontend
npm test
# 覆盖 53 个 Vitest 测试套件（状态管理 auth/category/chat/market/sync、离线加密 SQLite 与同步、
# AI 深度思考/分析面板/实体微卡片/@账户提及/全量流式、Scheduler/Skills/MCP/RBAC、网络层与移动端）

cd ../gateway
npm run test:adapters
# 覆盖 11 个 Node 原生适配器测试（OneBot / WeChaty 桥接、群消息忽略、缓存与安全门禁）
```

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源协议发布。
