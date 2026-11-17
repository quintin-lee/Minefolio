# AI 财经早报持仓数据按用户隔离实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 AI 每日财经早报（市场脉搏）为“全局宏观摘要 + 用户账本持仓隔离映射”的两阶段架构，实现持仓资产绝对值脱敏、多账本 RBAC 权限隔离与高效按需生成。

**Architecture:** 
- **阶段一（公共大盘）**：新增 `market_news_digests` 全局表，每日定时抓取公共财经资讯并调用大模型提取宏观摘要及行业板块利好/利空标签，全站仅调用 1 次，零隐私泄漏；
- **阶段二（用户持仓）**：`ai_news_reports` 升级为按 `(user_id, ledger_id, report_date)` 唯一约束隔离。持仓数据在 Prompt 中进行绝对数值脱敏（仅暴露标的名称、分类/行业、持仓占比百分比，屏蔽真实金额与数量），并结合阶段一的板块影响矩阵进行诊断；
- **权限与按需触发**：HTTP 接口与服务层通过 `ctx_ledger_id()` 实施 RBAC 权限校验（未授权成员返回 1004），支持按需触发与缓存。

**Tech Stack:** C23, SQLite, PostgreSQL, csilk HTTP framework, Vue 3, TypeScript, Pinia.

**Spec:** [docs/superpowers/plans/2026-11-17-ai-news-portfolio-isolation.md](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/plans/2026-11-17-ai-news-portfolio-isolation.md)

## Global Constraints

- 编译验证必须使用内存受限环境：`./backend/tests/run_isolated.sh build` 及 `./backend/tests/run_isolated.sh unit/integration`。
- 所有 C 源码必须保持 `-Werror` 零告警，遵循 C23 规范与 `AGENTS.md` 命名分层规范。
- 数据库脚本遵循 Flyway 规范，提供 SQLite 与 PostgreSQL 双引擎迁移文件（`V028__*.sql`）。
- 任何发送给外部 LLM 的 Prompt 中禁止包含用户的实际持仓金额、数量或成本。

## Review Focus

1. **持仓隐私脱敏**：验证送入大模型 Prompt 的持仓 JSON 绝对不含 `current_value`、`cost_basis`、`quantity` 原始金额，且百分比之和归一化（总和为 100%）。
2. **多账本与成员权限越权**：验证非账本成员尝试获取或触发早报时被 `ctx_ledger_id()` 正确拦截，返回 1004。
3. **不同账本独立性**：同一个用户拥有账本 1 和账本 2 时，两个账本生成的早报持仓分析互不干扰。
4. **两阶段复用与防重复调用**：当日已存在 `market_news_digests` 时，触发用户持仓早报不再重复调用宏观新闻抓取和提炼。
5. **双引擎兼容性**：验证 SQLite 和 PostgreSQL 均能平滑迁移并在事务中运行。

---

### Task 1: 数据库迁移（V028 双引擎脚本）

**Files:**
- Create: `backend/sql/migrations/sqlite/V028__ai_news_portfolio_isolation.sql`
- Create: `backend/sql/migrations/postgres/V028__ai_news_portfolio_isolation.sql`
- Modify: `backend/sql/migration.sql`
- Modify: `backend/sql/migration_postgres.sql`

- [ ] **Step 1: 编写 SQLite 迁移脚本 `V028__ai_news_portfolio_isolation.sql`**
  - 创建 `market_news_digests` 表（`id`, `digest_date UNIQUE`, `title`, `macro_summary`, `sector_impacts`, `sources`, `model_used`, `token_cost`, `created_at`）。
  - 创建临时表 `ai_news_reports_v2`，新增 `ledger_id`, `digest_id`, `holdings_snapshot` 列，唯一键设为 `UNIQUE(user_id, ledger_id, report_date)`。
  - 数据从旧表回填，默认 `ledger_id` 回填为对应用户的默认账本 ID。
  - 删除旧表重命名临时表，建立索引 `idx_news_reports_user_ledger` 与 `idx_news_digests_date`。

- [ ] **Step 2: 编写 PostgreSQL 迁移脚本 `V028__ai_news_portfolio_isolation.sql`**
  - 创建 `market_news_digests` 表（使用 `BIGSERIAL` 和 `TIMESTAMPTZ`）。
  - 为 `ai_news_reports` 增加 `ledger_id`, `digest_id`, `holdings_snapshot` 列。
  - 回填历史记录的 `ledger_id` 为用户默认账本。
  - 调整唯一约束为 `UNIQUE(user_id, ledger_id, report_date)`。
  - 创建相应索引。

- [ ] **Step 3: 同步更新全新安装基线 `migration.sql` 与 `migration_postgres.sql`**
  - 在全量初始化 SQL 中补充 `market_news_digests` 定义及 `ai_news_reports` 的新字段与约束。

- [ ] **Step 4: 运行隔离环境迁移验证**
  - 运行 `./backend/tests/run_isolated.sh unit` 确保数据库迁移引擎自动探测并成功执行 V028 迁移。

- [ ] **Step 5: 提交代码**
  - Git commit: `feat(db): add V028 migration for ai news two-stage digest and ledger isolation`

---

### Task 2: 后端数据脱敏与服务层重构（DDD Application & Services Layer）

**Files:**
- Modify: `backend/src/services/ai/ai_news_service.h`
- Modify: `backend/src/services/ai/ai_news_service.c`
- Modify: `backend/src/services/ai/tools/news_tool.c`
- Create: `backend/tests/unit/test_ai_news_privacy.c`

- [ ] **Step 1: 编写持仓脱敏与占比计算的单元测试 `test_ai_news_privacy.c`**
  - 验证资产列表输入（包含资产名称、类别、金额）转换为脱敏格式（包含 `name`, `category`, `weight`），且没有金额和数量字段。
  - 验证总市值计算及权重百分比计算准确性（如 50.0%、33.3% 等）。

- [ ] **Step 2: 实现公共大盘摘要生成逻辑 `ai_news_get_or_create_digest()`**
  - 检查当日 `market_news_digests` 是否已存在。
  - 若不存在，抓取今日要闻并调用大模型提取结构化大盘总结与行业影响（`sector_impacts`）。
  - 存入 `market_news_digests` 并返回 `digest_id`。

- [ ] **Step 3: 重构用户持仓报告生成 `ai_news_trigger_for_ledger()`**
  - 入参增加 `int64_t ledger_id`。
  - 查询指定账本下的投资类资产（`a.ledger_id = ? AND a.user_id = ?`）。
  - 执行持仓数据脱敏（Data Masking）：计算总金额后转化为百分比，只传递标的名、类别和占比。
  - 将公共大盘摘要及行业影响与脱敏持仓结合，拼入 Prompt 让 LLM 给出操作建议。
  - 更新 `ai_news_reports`，写入 `holdings_snapshot`、`affected_assets` 和 `analysis`。

- [ ] **Step 4: 更新大模型工具 `news_tool.c` 中的 `exec_get_market_pulse`**
  - 支持按 `user_id` 和上下文 `ledger_id` 查询报告，保持工具与账本隔离对齐。

- [ ] **Step 5: 运行隔离环境单元测试**
  - 编译并运行 `./backend/tests/run_isolated.sh unit`，确认所有测试（包括新写的 `test_ai_news_privacy`）通过。

- [ ] **Step 6: 提交代码**
  - Git commit: `feat(ai): refactor ai news to two-stage pipeline with portfolio data masking`

---

### Task 3: 控制器层与权限校验（Interfaces Layer & RBAC）

**Files:**
- Modify: `backend/src/interfaces/http/controllers/ai_news_controller.c`
- Modify: `backend/src/services/market/market_scheduler.c`
- Create: `backend/tests/test_ai_news_isolation.sh`

- [ ] **Step 1: 更新控制器 `ai_news_controller.c`**
  - `api_ai_news_list`: 调用 `ctx_ledger_id(c, user_id, "viewer")` 获取并校验 `ledger_id`，未授权直接 1004。查询时增加 `WHERE user_id=? AND ledger_id=?`。
  - `api_ai_news_get_date`: 支持可选 `ledger_id` 参数并校验权限。
  - `api_ai_news_trigger`: 调用 `ctx_ledger_id(c, user_id, "editor")` 校验账本编辑权限，传递 `ledger_id` 给服务层。

- [ ] **Step 2: 优化调度器 `market_scheduler.c` 的每日触发机制**
  - 在每日 08:00 定时触发时，仅调用 `ai_news_generate_daily_digest(pool)` 生成公共宏观摘要（阶段一）。
  - 用户持仓早报（阶段二）改为用户进入早报页面时“按需生成/懒加载”，杜绝全量并发调用 LLM 造成的资源耗尽。

- [ ] **Step 3: 编写端到端集成测试脚本 `backend/tests/test_ai_news_isolation.sh`**
  - 测试用例 1：公共早报生成；
  - 测试用例 2：用户 A 针对账本 1 触发生成早报，返回成功；
  - 测试用例 3：用户 B（非账本 1 成员）查询账本 1 早报返回 1004；
  - 测试用例 4：用户 A 针对账本 2 触发早报，生成独立的针对账本 2 持仓的报告；
  - 测试用例 5：检查生成的报告内容与 Prompt，确认不存在真实持仓金额，只包含脱敏百分比。

- [ ] **Step 4: 运行隔离集成测试**
  - 运行 `./backend/tests/run_isolated.sh -- ./backend/tests/test_ai_news_isolation.sh`。

- [ ] **Step 5: 提交代码**
  - Git commit: `feat(api): enforce ledger rbac and lazy generation on ai news endpoints`

---

### Task 4: 前端适配与多账本联动（Frontend UI & API）

**Files:**
- Modify: `frontend/src/api/ai-news.ts`
- Modify: `frontend/src/types/index.ts`
- Modify: `frontend/src/views/NewsPulse.vue`

- [ ] **Step 1: 更新 TypeScript 类型定义 `frontend/src/types/index.ts`**
  - 在 `AiNewsReport` 中补充 `ledger_id?: number`, `digest_id?: number`, `holdings_snapshot?: string`。

- [ ] **Step 2: 更新前端 API 客户端 `frontend/src/api/ai-news.ts`**
  - 接口支持传入可选的 `ledger_id`（若未传则由全局 `http.ts` 拦截器中的 `X-Ledger-Id` 自动注入当前激活账本）。

- [ ] **Step 3: 增强 `frontend/src/views/NewsPulse.vue` 视图**
  - 监听当前激活账本的变化，切换账本时自动刷新该账本对应的早报列表。
  - 优化持仓分析展示卡片，支持展示当前分析所针对的账本名称及脱敏后的仓位概览。

- [ ] **Step 4: 前端测试与构建验证**
  - 运行 `npm --prefix frontend test`。
  - 运行 `npm --prefix frontend run build` 确保前端 0 报错。

- [ ] **Step 5: 提交代码**
  - Git commit: `feat(frontend): adapt market pulse view to multi-ledger space and masked holdings`

---

### Task 5: 全面回归验证与清理

**Files:**
- Modify: `backend/tests/run_isolated.sh` (将 `test_ai_news_isolation.sh` 加入默认 integration 套件)

- [ ] **Step 1: 运行全量单元测试与内存隔离检查**
  - `./backend/tests/run_isolated.sh unit`

- [ ] **Step 2: 运行全量集成测试套件**
  - `./backend/tests/run_isolated.sh integration`

- [ ] **Step 3: 运行完整构建构建**
  - `cmake --build backend/build --parallel 2 && npm --prefix frontend run build`

- [ ] **Step 4: 提交并准备交付**
  - Git commit: `test: integrate test_ai_news_isolation into regression suite`
