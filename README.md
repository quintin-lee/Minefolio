# Minefolio

[![Release: v1.0.0](https://img.shields.io/badge/Release-v1.0.0-success.svg)](https://github.com/quintin-lee/Minefolio/releases/tag/v1.0.0)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/C-23-00599C?logo=c)](https://en.cppreference.com/w/c/23)
[![Vue 3](https://img.shields.io/badge/Vue-3.5-4FC08D?logo=vue.js)](https://vuejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.6-3178C6?logo=typescript)](https://www.typescriptlang.org/)
[![Vite](https://img.shields.io/badge/Vite-5.4-646CFF?logo=vite)](https://vitejs.dev/)
[![SQLite](https://img.shields.io/badge/SQLite-WAL-003B57?logo=sqlite)](https://www.sqlite.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-16+-4169E1?logo=postgresql)](https://www.postgresql.org/)

Minefolio 是一款极度轻量、安全、专业的**开源全资产管理与投资追踪系统**。
后端采用现代化 **C23 标准** 及高性能 **csilk 框架** 开发，常驻内存仅约 15MB，单节点吞吐达数万 QPS；前端基于 **Vue 3 + TypeScript** 与 **Element Plus** 构建，支持桌面端与移动端（Capacitor + SQLite WASM 纯离线运行）。

---

## 🌟 核心特性与亮点

* 🧮 **高精度金融核心与定点数引擎 (Financial Core)**：基于 64 位有符号整型定点数与标准舍入算法，彻底消除浮点数计算误差，支持高精度货币 (Money)、持仓数量 (Quantity)、成交价 (Price)、汇率 (Rate)、百分比 (Percentage) 与盈亏 (PnL) 严谨核算。
* 🏛️ **统一账本状态引擎 (Ledger Engine)**：确立「Transaction 是唯一金融事实」准则，资产持仓、现金余额、成本基础、已实现/浮动盈亏全由账本引擎统一计算与更新，并提供基于时间序列事实全量重建 (`rebuild`) 的自愈能力。
* 🛡️ **企业级 AI 架构与五级风控引擎 (AI Framework & Risk Engine)**：AI 系统解耦为运行时 (Runtime)、模型适配 (Model)、DAG 工作流 (Workflow)、领域工具集 (Tools)、安全策略 (Policy) 与调用追踪 (Trace)；实行五级风险评估与带 Nonce 防重放的恒定时间 HMAC-SHA256 双重确认令牌机制。
* 🔑 **统一 Secret Provider 与零硬编码安全门禁**：建立统一敏感配置检索机制，原生兼容环境变量、Docker/K8s Secret 文件挂载与外部密钥管理器 (Vault/AWS/K8s)；在生产启动期实施硬编码/弱口令主动熔断。
* 💼 **全资产全场景覆盖**：支持现金、银行储蓄、股票、基金、债券、加密货币、房产、贷款、信用卡等资产与负债统一建档，支持负债方向自动正负号翻转。
* 📈 **专业资管级投资模型**：加权买入成本、分红摊薄计价、卖出部分成本按比例核减、手续费联动与基于 `parent_tx_id` 的级联回滚，精确计算已实现与浮动盈亏。
* 🌐 **多币种实时外汇引擎 (FX Engine)**：支持全币种资产管理，内置 Yahoo Finance 实时汇率同步与手动设置，支持基准折算净资产实时呈现。
* 📊 **外汇损益双因子归因分析**：在报表中心将境外资产收益严格拆解为「**资产标的自身价格涨跌**」与「**纯外汇汇率波动汇兑损益 (FX PnL)**」。
* 👥 **多账本空间与 RBAC 权限管理**：支持个人、家庭、企业多账本独立隔离，支持邀请码一键入账本，严格实行 `Owner`、`Editor`、`Viewer` 空间鉴权。
* 🎯 **智能定投计划 (DCA) 与现金流日历**：支持周期性定投计划排程与一键确认买入；支持分红/利息/租金排程推演与 30/90 天现金流预测日历。
* 📸 **智能票据 OCR 识别与录单**：桌面端与移动端支持调用 OpenAI/DeepSeek 视觉多模态大模型解析发票与小票，内置纯离线启发式规则兜底，一键拍照自动填单并智能匹配分类。
* ⚡ **多源行情定时同步引擎**：对接 A股/公募基金/Yahoo Finance/Binance 行情接口，支持防抖定时调度、交易时段自动刷新与 HTTP 代理连通性检测。
* 🤖 **AI 财务助理与全链路追踪 (AI Traces)**：多轮财务上下文对话、财务函数 Tool Calling 自动化执行，全量追踪会话耗时、Token 消耗、模型供应商与工具调用 Spans。
* 🔐 **银行级安全与单点登录 (SSO)**：全链路 RSA-OAEP 前端口令加密传输、JWT 版本号即时吊销、TOTP 2FA 动态双因素认证、支持 GitHub 与通用 OIDC 单点登录。
* 📱 **双端同构与移动端离线 SQLite (WASM)**：基于 Capacitor 与内置 base64 嵌入式 `sql.js` WASM SQLite 引擎，手机端无网环境下依然畅享完整记账体验。
* 🔬 **框架级可观测性管理后台**：内置 `/csilk-admin` 性能仪表盘，支持实时 RPS 与 HTTP 状态监控、工作流 Universal DAG 动态拓扑图可视化，以及 100Hz CPU 堆栈火焰图 Profiler。

---

## 🛠️ 技术栈

| 层次 | 技术选型 | 说明 |
| :--- | :--- | :--- |
| **后端语言 & 标准** | C23 (GCC 14 / Clang 18) | 原生编译，极低资源占用（~2.8MB 二进制，~15MB 内存） |
| **Web 框架** | [csilk](https://github.com/quintin-lee/csilk) v0.5.2 | 高性能现代 C 异步 HTTP/HTTP2 框架 |
| **数据库** | SQLite 3 (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移 |
| **JSON 解析** | yyjson | 超高性能 C 语言 JSON 序列化/反序列化库 |
| **前端架构** | Vue 3 + TypeScript + Vite | 严格模式 `vue-tsc -b` 0 错误编译 |
| **UI 组件库** | Element Plus + Iconify (Phosphor Icons) | Glassmorphism 科技感暗色/渐变主题 |
| **数据可视化** | ECharts 5 + Mermaid.js | 净资产走势、资产占比、外汇走势、工作流 DAG 拓扑图 |
| **移动端离线存储** | Capacitor 6 + sql.js (WASM) | 离线纯前端 SQLite 数据库 |
| **安全机制** | RSA-OAEP + SHA-256 + HMAC-SHA256 JWT + TOTP | 密码防截获重放、2FA 硬件防撞 |

---

## 🚀 快速启动

### 方式 1：Docker Compose 容器化部署（推荐）

```bash
# 1. 克隆代码仓库
git clone https://github.com/quintin-lee/Minefolio.git
cd Minefolio

# 2. 配置环境变量
cp .env.example .env
# 编辑 .env 文件，务必修改 MINEFOLIO_JWT_SECRET 密钥
vim .env

# 3. 启动多容器服务
docker compose up -d --build
```
启动后访问 `http://localhost` 即可直接使用。

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
│   ├── CMakeLists.txt            # 构建配置（C23、csilk、yyjson）
│   ├── sql/                      # 数据库迁移脚本（SQLite 与 PostgreSQL）
│   │   ├── migration.sql         # 16 张核心数据表与默认种子
│   │   └── migration_postgres.sql
│   ├── src/
│   │   ├── main.c                # 服务主入口、中间件装配、路由注册与静态托管
│   │   ├── core/                 # 金融核心与基础引擎
│   │   │   ├── financial/        # 定点数运算库（money, decimal, quantity, price, rate, currency, pnl）
│   │   │   └── ledger/           # 统一账本状态计算引擎（状态推演、事实重放、资产重建）
│   │   ├── controllers/          # 表现层：解析参数、调用服务、封装响应
│   │   ├── services/             # 业务层：状态编排、复杂记账逻辑、多币种折算
│   │   │   ├── ai/               # 企业级解耦 AI 架构（runtime, model, workflow, tools, policy, trace）
│   │   │   └── market/           # 行情引擎、外汇服务、调度器
│   │   ├── repositories/         # 数据层：参数化 SQL 查询，返回 csilk_json_t*
│   │   ├── common/               # 公共组件（db, jwt, balance, response, 2fa, ocr）
│   │   ├── config/               # 密钥对管理、数据库配置、统一 Secret Provider (secret.h/.c)
│   │   └── middlewares/          # JWT 鉴权、CORS、CSRF、限流与安全头中间件
│   └── tests/                    # 单元测试与集成测试矩阵
│       ├── unit/                 # 13 项高覆盖 CTest 单元测试（金融定点数、账本数学、AI工具/策略、Secret Provider）
│       ├── test_link.sh          # 核心业务闭环测试 (38 用例，139 断言)
│       ├── test_ledgers.sh       # 多账本与 RBAC 测试 (16 用例)
│       ├── test_2fa.sh           # TOTP 双因素认证测试 (12 用例)
│       ├── test_dca_cashflow.sh  # 定投与现金流日历测试 (18 用例)
│       ├── test_ai_trace.sh      # AI 会话与全链路追踪测试 (17 用例)
│       ├── test_market_sync.sh   # 实时行情多源同步测试 (18 用例)
│       └── test_fx_oauth.sh      # 汇率、汇兑损益、OCR、OAuth 单点登录测试 (20 用例)
├── frontend/                     # Vue 3 现代化前端
│   ├── src/
│   │   ├── api/                  # 强类型 API 接口封装（桌面端与移动端共享）
│   │   ├── stores/               # Pinia 状态管理（auth, category, chat, sync）
│   │   ├── views/                # 桌面端页面组件（Dashboard, Assets, Reports, Plans...）
│   │   ├── views-mobile/         # 移动端优化轻量视图（卡片流、OCR扫码抽屉、PlansMobile）
│   │   ├── components/           # ECharts 图表、DAG 可视化、扫描弹窗
│   │   ├── db/                   # sql.js WASM 离线 SQLite 驱动
│   │   ├── router/               # 桌面与移动端独立双路由
│   │   └── types/                # 全局 TypeScript 接口模型定义
│   └── vite.config.ts            # Vite 桌面与移动端多模式打包配置
├── scripts/                      # 快捷开发与构建脚本
│   ├── dev.sh                    # 一键启动前后端双开发服务
│   └── build.sh                  # 一键生成 Release 生产部署包
├── Dockerfile                    # 多阶段自动化构建镜像
└── docker-compose.yml            # 生产级多容器编排（Minefolio + Nginx）
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
| `POST` | `/api/auth/2fa/generate` | JWT | 生成 TOTP 双因素认证密钥与绑定二维码 |
| `POST` | `/api/auth/2fa/enable` | JWT | 校验动态码并正式启用 2FA |
| `POST` | `/api/auth/2fa/disable` | JWT | 关闭 2FA 双因素保护 |
| `POST` | `/api/auth/2fa/login` | 公开 | 提交 2FA 动态码完成登录 |
| `GET` | `/api/auth/oauth/providers` | 公开 | 获取启用的第三方单点登录源（GitHub / OIDC） |
| `POST` | `/api/auth/oauth/callback` | 公开 | 第三方授权回调与自动 Provision 用户登录 |

### 2. 多账本空间管理 (Ledgers & Collaboration)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET` | `/api/ledgers` | JWT | 获取当前用户加入的所有账本空间 |
| `POST` | `/api/ledgers` | JWT | 创建新的独立账本（支持指定币种与图标） |
| `GET/PUT/DELETE` | `/api/ledgers/:id` | JWT | 获取、更新或解散指定的账本空间 |
| `GET/POST` | `/api/ledgers/:id/members`| JWT | 获取成员列表或添加成员 |
| `POST` | `/api/ledgers/:id/invite-code`| JWT | 生成加入账本的有效期邀请码 |
| `POST` | `/api/ledgers/join` | JWT | 凭邀请码加入目标账本 |

### 3. 资产、负债与交易 (Assets & Transactions)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `GET/POST` | `/api/assets` | JWT | 资产列表（分页/过滤）与新建资产 |
| `GET/PUT/DELETE` | `/api/assets/:id` | JWT | 资产详情、编辑资产净值、删除资产 |
| `POST` | `/api/assets/:id/rebuild` | JWT | 账本引擎：重放单标的事实交易，全量重建持仓量、成本与市值 |
| `POST` | `/api/assets/rebuild` | JWT | 账本引擎：全量重建用户所有投资资产的持仓与成本状态 |
| `GET/POST` | `/api/transactions` | JWT | 交易记录（买入/卖出/存取/分红，支持 fee 联动） |
| `PUT/DELETE` | `/api/transactions/:id` | JWT | 编辑或删除交易（带 fee 子行级联回滚） |
| `GET/POST` | `/api/daily-expenses` | JWT | 日常记账收支列表与创建收支 |
| `POST` | `/api/transfers` | JWT | 账户间跨资产转账（自动生成配对流水） |

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
| `POST` | `/api/market/sync` | JWT | 批量刷新当前用户持仓资产的市场价格与净值 |
| `GET` | `/api/market/exchange-rates` | JWT | 获取多币种实时外汇对汇率表 |
| `POST` | `/api/market/exchange-rates` | JWT | 手动设置与更新特定外汇汇率 |
| `GET` | `/api/market/fx-history` | JWT | 获取指定外币兑基准币的历史走势快照 |
| `GET` | `/api/summary` | JWT | 仪表盘资产总额、负债、净资产总览 |
| `GET` | `/api/reports/multi-currency-summary` | JWT | 多币种资产构成与指定基准币种一键折算报表 |
| `GET` | `/api/reports/fx-pnl` | JWT | 境外外币资产「价格盈亏 vs 汇率损益」剥离归因报表 |
| `GET` | `/api/reports/holdings` | JWT | 投资持仓明细（持仓量、摊薄成本、浮动盈亏） |

### 6. 智能识别、智能规则与 AI 助理 (Smart Features & AI)
| 方法 | 路径 | 鉴权 | 说明 |
| :--- | :--- | :---: | :--- |
| `POST` | `/api/receipts/scan` | JWT | 发票/收据图片 OCR 多模态智能识别与自动分类填单 |
| `GET/POST` | `/api/import-rules` | JWT | 查看与配置商户/关键词智能记账规则 |
| `POST` | `/api/import-rules/reset-defaults` | JWT | 一键重置恢复系统推荐的智能分类规则库 |
| `GET` | `/api/ai/workflows` | JWT | 获取内置智能财务分析工作流模板与步骤说明 |
| `POST` | `/api/ai/workflows/:id/run` | JWT | 启动执行指定智能工作流（SSE 流式实时推送执行事件） |
| `POST` | `/api/ai/chat` | JWT | 与 AI 财务分析助理多轮上下文对话（支持 Tool Calling） |
| `GET` | `/api/ai/traces` | JWT | 查询 AI 会话的全链路耗时、Token 消耗与调用 Spans |
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

---

## 🧪 自动化测试体系

Minefolio 拥有完整的双层测试矩阵（单元测试 + 端到端集成回归）：

### 1. CTest 单元测试矩阵（13 大测试套件，高频毫秒级断言）
```bash
cd backend/build
ctest --output-on-failure
# 覆盖核心模块：
# - 金融核心定点数：test_currency, test_decimal, test_money, test_quantity, test_price, test_rate, test_pnl, test_fx
# - 统一账本数学与引擎：test_ledger_math, test_ledger_engine
# - 智能架构与风控系统：test_ai_tools, test_ai_policy
# - 统一机密提供者与安全门禁：test_secret_provider
```

### 2. 端到端集成测试矩阵（7 大测试套件，覆盖 139+ 真实断言）
```bash
./backend/tests/test_link.sh && \
./backend/tests/test_ledgers.sh && \
./backend/tests/test_2fa.sh && \
./backend/tests/test_dca_cashflow.sh && \
./backend/tests/test_ai_trace.sh && \
./backend/tests/test_market_sync.sh && \
./backend/tests/test_fx_oauth.sh
```

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源协议发布。
