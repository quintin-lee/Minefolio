# Minefolio 实施状态与扩展指南（v1.5.0）

> **状态**：当前有效。本文件原为 2026-08-10 的 MVP 实施计划，所列阶段已全部完成并被后续迭代大幅扩展，现原地重写为「已实现能力 + 扩展指南」。
> **目标**：实现前后端分离的全资产管理与投资追踪系统。
> **架构**：C23 + csilk 分层后端（Interfaces → Application → Domain → Infrastructure，辅以 Financial Core 与 Ledger Engine）+ Vue 3 SPA 桌面端 + Capacitor/sql.js 离线移动端。
> **相关**：现状设计见 `docs/superpowers/specs/2026-08-10-minefolio-design.md`；工程级细节见 `docs/architecture.md`。

---

## 1. 已实现能力清单（对照原 MVP 阶段）

| 原计划阶段 | 状态 | 当前实现位置 / 说明 |
|-----------|------|-------------------|
| 1 DB 初始化 + 分类 CRUD | ✅ 已完成并演进 | 版本化迁移 V001~V038（SQLite/PostgreSQL 双方言），`domain/category/`、`application/category/`、分类树由 controller 内哈希表组装 |
| 2 认证（注册/登录/JWT） | ✅ 已完成并大幅扩展 | 2FA、OAuth/OIDC、RSA-OAEP 前端加密、JWT 版本吊销、用户管理、统一 Secret Provider |
| 3 资产 CRUD | ✅ 已完成并演进 | 账户分组、归档/排序、账户与字段 RBAC、资产重建与余额日志 |
| 4 交易记录 API | ✅ 已完成并演进 | 复式账本引擎、FIFO/加权均价成本、量价精度、显式转账交易、级联回滚 |
| 5 日常收支 + 月度汇总 + 标签 | ✅ 已完成 | `domain/{daily_expense,tag}/`、月度/分类/标签报表 |
| 6 汇总统计 + 报表 | ✅ 已完成并扩展 | 16 个报表端点、多币种汇总、HHI 集中度、外汇损益双因子归因 |
| 7 前端基础框架 | ✅ 已完成 | Vite + TS + Element Plus + Pinia + vue-router + i18n（中/英） |
| 8 登录页 + auth store | ✅ 已完成 | 另含 Setup 向导、OAuth 回调、2FA 设置 |
| 9 分类管理页 | ✅ 已完成 | 树形 CRUD + 性能优化（V036/V037） |
| 10 标签管理页 | ✅ 已完成 | CRUD + 颜色 + 自动补全 |
| 11 资产列表 | ✅ 已完成 | 新增持仓页、资产详情页、账户卡片分组 |
| 12 交易记录页 | ✅ 已完成 | 筛选 + 月度视图 |
| 13 日常收支记账页 | ✅ 已完成 | 标签选择 + 月度/年度图表 |
| 14 Dashboard | ✅ 已完成 | Bento 布局 + 净资产/资产/现金流图表 |
| 15 报表中心 | ✅ 已完成 | 全部报表入口 + 图表 |
| 16 转账 | ✅ 已完成 | 账本级联、显式交易对 |
| 17 生产构建 + 部署 | ✅ 已完成 | Docker Compose 双容器 + Nginx + 可选 PostgreSQL |

**原计划 YAGNI 排除项中已实际落地**：多用户/多账本协作、移动端 App、AI 财务助理、Excel/CSV 导入导出、多数据源行情同步。

---

## 2. 后续迭代已交付的关键能力

| 能力 | 交付内容 |
|------|---------|
| Financial Core + Ledger Engine | 128 位定点精度、强类型货币量价、事件溯源账本、检查点快照增量重建、Point-in-Time FX |
| 统一 AI Runtime | Session / Context / Model / Tool / Policy / Workflow / Trace / Memory 八子系统，流式 + 同步 Agent 循环 |
| AI 工具与工作流 | 25 个内置工具注册项 + 动态 MCP 工具；13 个内置 DAG 工作流（含条件分支与 Saga 补偿） |
| AI 风控 | 五级风险评估、双重确认 + nonce 防重放、提示注入防御、工具内容隔离 |
| MCP 生态 | 动态接入远程 MCP Server（stdio / HTTP）、对外 `/mcp` JSON-RPC 服务 |
| Agent Skills | V022 技能库，Markdown 导入导出、`list_skills` / `use_skill` |
| 多账本与 RBAC | Owner/Editor/Viewer、账户级/字段级权限、AI/报表视野过滤 |
| DCA / 现金流日历 | 定投计划、分红/利息/租金排程、30/90 天预测 |
| 行情与调度中心 | 多源行情、汇率同步、5 段式 Cron、动态休眠工作池、Webhook/微信推送 |
| 移动端 | Capacitor 6 + sql.js WASM 离线 SQLite + 同步 + AI 对话 |
| 异步任务与运维 | `async_tasks` 队列、在线热备份、WAL checkpoint、`reset-password`、AI trace 管理后台 |

---

## 3. 关键架构决策记录

1. **后端采用 C23 + csilk 分层架构**：通过 FetchContent 引入 csilk（本地 `deps/csilk` 优先），源码按 `interfaces / application / domain / infrastructure / core / services / common` 划分，Domain 零外部依赖。
2. **金融计算禁用浮点**：`decimal_t` 用 `__int128` 定点数，领域层全程高精度，仅 JSON DTO 边界回落 `double`；`decimal.c` 豁免 `-Wpedantic`。
3. **Transaction 为唯一金融事实**：所有余额/持仓/盈亏由账本引擎推导，支持从检查点增量重建，改账级联失效下游快照。
4. **双数据库方言**：SQLite（WAL）与 PostgreSQL（libpq 可选）共用 38 个版本迁移脚本；生产禁止静默降级为 mock 驱动。
5. **AI 与业务解耦**：Controller / Workflow 不直接调用 LLM，统一经 AI Runtime；工具区分只读/变更，internal-only 工具不暴露给模型。
6. **默认安全**：启动强制校验 `JWT_SECRET` 与非弱口令；CORS 未配置时返回 `*` 且禁凭证；CSRF 可选开关；认证写接口限流。
7. **双端同构**：桌面 SPA 与移动 Capacitor 共用 API 层与大部分组件，移动端以本地 SQLite + 同步实现离线。

---

## 4. 常见扩展任务操作清单

### 4.1 新增一个 HTTP API
- [ ] `application/<domain>/` 新增 usecase / command / dto。
- [ ] `domain/<domain>/` 增加纯规则（`rules.c`）与仓储接口（`repository.h`）。
- [ ] `infrastructure/repositories/<name>_repo_impl.c` 实现仓储（参数化 SQL）。
- [ ] `interfaces/http/controllers/<name>_controller.c` 用 `csilk_app_{get,post,put,delete}_ext` 注册路由并声明请求/响应 schema。
- [ ] 在 `backend/src/main.c` 调用 `register_<name>_routes(app)`。
- [ ] 在 `backend/tests/unit/` 增加领域/用例测试，并在 `CMakeLists.txt` 注册。

### 4.2 新增一个 AI 工具
- [ ] 在 `services/ai/tools/` 定义 `ai_tool_t`（name、description、parameters_schema、execute、validate）。
- [ ] 明确 `is_mutation` 与 `internal_only`：确认类工具必须 `internal_only`，绝不暴露给 LLM。
- [ ] 在对应 `*_register_all()` 中调用 `ai_tool_register`。
- [ ] 补充 `test_ai_tools.c` / `test_ai_policy.c` 用例，验证只读/变更视图与 Policy 拦截。

### 4.3 新增一个 AI 工作流
- [ ] 在 `services/ai/workflows/` 构造 `ai_workflow_graph_t`（节点、边、条件分支、补偿）。
- [ ] 在 `services/ai/workflow/engine.c` 的 `ensure_init_builtin_workflows()` 中注册。
- [ ] 补充 `test_workflow_branching.c` / `test_workflow_step_error.c` 用例。

### 4.4 新增数据库迁移
- [ ] 在 `sql/migrations/sqlite/` 与 `sql/migrations/postgres/` 各新增同名 `V<NNN>__<name>.sql`。
- [ ] 保持双方言语义一致，避免 SQLite 特有语法进入 Postgres 脚本（反之亦然）。
- [ ] 通过 `test_migration_engine.c` / `test_database_repository.c` 验证。
- [ ] 更新设计文档中的迁移版本范围与表数量描述。

### 4.5 新增前端页面
- [ ] `frontend/src/api/` 增加 API 模块，统一经 `utils/http.ts`。
- [ ] `frontend/src/views/` 增加视图并在 `router/index.ts` 注册（评估是否需要 `requiresAuth`）。
- [ ] 需要离线能力时，在 `db/schema.ts` 增加本地表与同步逻辑。
- [ ] 运行 `npm run typecheck` 与 `npm test`。

---

## 5. 质量门禁

| 检查 | 命令 |
|------|------|
| 后端构建 | `cmake -S backend -B backend/build && cmake --build backend/build` |
| 后端测试 | `ctest --test-dir backend/build --output-on-failure` |
| 内存/UB 检查 | `cmake -S backend -B backend/build-asan -DMINEFOLIO_SANITIZE=ON && cmake --build backend/build-asan && ctest --test-dir backend/build-asan` |
| 格式检查 | 构建时由 `format_sources` 目标依据 `.clang-format` 自动校验（需 clang-format ≥ 14） |
| 前端类型检查 | `npm run typecheck`（`vue-tsc -b`，要求 0 错误） |
| 前端测试 | `npm test`（Vitest） |
| 前端构建 | `npm run build` / `npm run build:mobile` |

> 说明：`config/ai.json` 被 `.gitignore` 排除，CI 干净克隆中不存在；相关测试自建临时配置并用 `AI_CONFIG` 覆盖，不依赖构建目录中的 `config/`。多数依赖 DB 的测试通过 `copy_assets` 目标取得 SQL 脚本。

---

## 6. 已知约束与后续方向

- **历史文档**：`docs/superpowers/{specs,plans}/` 中的其他文件是单次功能演进的历史档案，不代表当前状态；判断现状请以本文、`docs/architecture.md` 与代码为准。
- **双引擎一致性**：SQLite/PostgreSQL 各 38 份迁移需持续保持语义对齐，建议纳入 CI 校验。
- **AI 安全区**：风控、确认、nonce、提示注入防御属于安全关键路径，改动时必须补齐对应专项测试。
- **双端覆盖**：前端测试当前统一使用移动端 Vitest 配置，桌面专属逻辑的独立测试覆盖可继续加强。
- **可观测性**：AI trace、`/csilk-admin` 仪表盘与工作流 DAG 可视化已具备，可进一步接入外部监控体系。
