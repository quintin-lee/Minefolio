# Minefolio 代码审查改进项分阶段修复计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 逐项修复项目全栈代码审查中发现的内存泄漏（DEF-01）、数据导入导出账本隔离缺失（DEF-02/03）、资产权重跨账本混淆（DEF-04）、控制器直连 SQL 分层违规（DEF-05）及移动端初始化引导缺陷（DEF-06）。

**Architecture:** 
- 内存资源管理：使用无堆分配开销的 `csilk_db_exec_param()` 替换无返回集需求的 `csilk_db_query_param_json()`，彻底根除孤儿 JSON 对象堆泄漏。
- 账本隔离闭环：在导入导出接口与用例中引入 `ctx_ledger_id()` 鉴权与参数注入，实现导入数据强制关联指定账本、导出数据限制在当前账本内，并修正资产详情的单账本权重聚合口径。
- DDD 分层收敛：将散落在 Controller 中的 SQL 查询下沉到对应模块的仓储层与用例层。

**Tech Stack:** C23, SQLite, PostgreSQL, csilk HTTP framework, Vue 3, TypeScript.

**Spec:** [docs/codebase_review_report.md](file:///home/quintin/.gemini/antigravity-cli/brain/be9af027-0fd2-410e-9dec-86528f476c4a/codebase_review_report.md)

## Global Constraints

- 所有编译与测试必须严格在内存隔离环境运行：`./backend/tests/run_isolated.sh build` 与 `./backend/tests/run_isolated.sh unit/integration`。
- 所有 C 源码必须保持 `-Werror` 零编译告警。
- 保持 SQLite 与 PostgreSQL 双引擎兼容性。
- 每个 Task 完成后独立验证并在隔离环境下运行对应测试，分阶段执行 Git commit。

## Review Focus

1. **内存零泄漏**：确保写操作改用 `csilk_db_exec_param` 后，返回值判断正确，且不会产生未释放的堆对象。
2. **多账本数据一致性**：验证 CSV 导入日常收支后，能够被 `GET /api/daily-expenses?ledger_id=N` 正确查出，并且不同账本间互不干扰。
3. **导出数据不越界**：验证 CSV 导出时仅包含指定账本记录，未授权用户无法导出他人账本数据。
4. **DDD 架构边界纯洁**：控制器层不再包含任何原生 SQL 字符串或 `csilk_db_query_*` 调用。
5. **回归测试全绿**：70 项单元测试与 14 项集成测试全部通过。

---

### Task 1: 修复未捕获并回收的 query 调用，杜绝堆内存泄漏 (DEF-01)

**Files:**
- Modify: `backend/src/infrastructure/repositories/ledger_repo_impl.c:290-310`
- Modify: `backend/src/services/ai/ai_news_service.c:228, 407, 424, 547, 581`
- Modify: `backend/src/services/market/exchange_rate_service.c:290, 318`
- Modify: `backend/src/interfaces/http/controllers/import_rule_controller.c:199`

- [x] **Step 1: 修复 `ledger_repo_impl.c` 中的 7 处级联删除内存泄漏**
  - 在 `mf_ledger_repo_delete` 中，将 7 处 `csilk_db_query_param_json(pool, "DELETE FROM ... WHERE ledger_id = ?", ...)` 改为 `csilk_db_exec_param(pool, "DELETE FROM ...", ...)`。
- [x] **Step 2: 修复 `ai_news_service.c` 中的 5 处写操作内存泄漏**
  - 检查并修改 `ai_news_get_or_create_digest`、`ai_news_execute_analysis` 等函数中的 DML 调用，改用 `csilk_db_exec_param()`。
- [x] **Step 3: 修复 `exchange_rate_service.c` 中的 2 处汇率持久化内存泄漏**
  - 在 `exchange_rate_service_fetch_today` 与 `exchange_rate_store` 中，将执行 `exchange_rate_upsert_sql()` 的调用改用 `csilk_db_exec_param()`。
- [x] **Step 4: 修复 `import_rule_controller.c` 中的 1 处规则删除内存泄漏**
  - 改用 `csilk_db_exec_param()` 或将其下沉至仓储层。
- [x] **Step 5: 隔离环境编译并运行单元测试验证**
  - 运行 `./backend/tests/run_isolated.sh build backend/build`
  - 运行 `./backend/tests/run_isolated.sh unit backend/build`
- [x] **Step 6: 提交代码**
  - Git commit: `fix(core): replace uncollected query_param_json with exec_param to prevent heap leaks`

---

### Task 2: 修复 CSV 导入日常收支遗漏 ledger_id 问题 (DEF-02)

**Files:**
- Modify: `backend/src/services/import_service.c:540-570`
- Modify: `backend/src/application/import_export/usecases.c`
- Test: `backend/tests/test_link.sh`

- [x] **Step 1: 更新 `services/import_service.c` 中的日常收支导入逻辑**
  - 在 `daily_expenses_import_csv` 中通过 `ctx_ledger_id(c, user_id, "editor")` 获取并校验当前激活账本；未授权（返回 <= 0）时直接拦截并返回 1004。
  - 将 `INSERT INTO daily_expenses` 改为显式包含 `ledger_id` 字段：
    `INSERT INTO daily_expenses (user_id, ledger_id, category_id, asset_id, expense_type, amount, currency, expense_date, note) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
- [x] **Step 2: 更新交易导入中的 `ledger_id` 显式透传**
  - 在 `transactions_import_csv` 中同样调用 `ctx_ledger_id(c, user_id, "editor")` 校验账本编辑权限，并在构造 `ledger_tx_t ltx` 时设置 `ltx.ledger_id = ledger_id`。
- [x] **Step 3: 在 `backend/tests/test_link.sh` 中验证导入数据的账本归属**
  - 验证 CSV 导入日常收支后，通过 `GET /api/daily-expenses?ledger_id=1` 能查到导入的数据，而在 `ledger_id=2` 中查不到。
- [x] **Step 4: 隔离环境编译并测试验证**
  - `./backend/tests/run_isolated.sh build backend/build`
  - `./backend/tests/run_isolated.sh -- bash backend/tests/test_link.sh`
- [x] **Step 5: 提交代码**
  - Git commit: `fix(import): enforce ledger isolation and persist ledger_id on csv import`

---

### Task 3: 修复 CSV 导出按当前账本空间进行数据隔离 (DEF-03)

**Files:**
- Modify: `backend/src/services/export_service.c:20-40, 150-175`
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: 在 `transactions_export_csv` 中增加账本范围过滤**
  - 调用 `ctx_ledger_id(c, user_id, "viewer")` 校验只读权限。
  - 查询语句由 `WHERE t.user_id=?` 改为 `WHERE t.user_id=? AND t.ledger_id=?`，参数追加 `lid_str`。
- [ ] **Step 2: 在 `daily_expenses_export_csv` 中增加账本范围过滤**
  - 调用 `ctx_ledger_id(c, user_id, "viewer")` 校验只读权限。
  - 查询语句由 `WHERE de.user_id=?` 改为 `WHERE de.user_id=? AND de.ledger_id=?`，参数追加 `lid_str`。
- [ ] **Step 3: 更新相关测试**
  - 确认带有 `X-Ledger-Id` 或 `?ledger_id=` 请求头时导出指定账本数据；跨账本越权时被正确拦截。
- [ ] **Step 4: 隔离环境编译并测试验证**
  - `./backend/tests/run_isolated.sh -- bash backend/tests/test_link.sh`
- [ ] **Step 5: 提交代码**
  - Git commit: `fix(export): scope transaction and expense csv export to active ledger`

---

### Task 4: 修复资产详情中的组合权重跨账本混淆 (DEF-04)

**Files:**
- Modify: `backend/src/application/asset/usecases.c:585-615`
- Test: `backend/tests/unit/test_account_usecases.c` 或针对性单元测试

- [ ] **Step 1: 修正 `asset_usecase_detail` 中的资产总值聚合口径**
  - 从已有资产记录中提取 `ledger_id`（若未指定或 <=0，使用资产关联的账本 ID）。
  - 将 `total_mkt` 计算的 SQL 改为按账本过滤：
    `SELECT a.current_value FROM assets a JOIN categories c ON a.category_id=c.id WHERE a.user_id=? AND a.ledger_id=? AND c.asset_type NOT IN ('loan','credit_card','other_liability')`
- [ ] **Step 2: 隔离环境编译与单元测试验证**
  - `./backend/tests/run_isolated.sh unit backend/build`
- [ ] **Step 3: 提交代码**
  - Git commit: `fix(asset): compute asset portfolio weight within owning ledger scope`

---

### Task 5: 重构控制器层直连 SQL 逻辑，巩固 DDD 分层边界 (DEF-05)

**Files:**
- Modify: `backend/src/infrastructure/repositories/auth_repo_impl.c/.h`
- Modify: `backend/src/interfaces/http/controllers/admin_controller.c`
- Modify: `backend/src/infrastructure/repositories/category_repo_impl.c/.h`
- Modify: `backend/src/interfaces/http/controllers/receipt_controller.c`
- Modify: `backend/src/infrastructure/repositories/import_rule_repo_impl.c/.h`
- Modify: `backend/src/interfaces/http/controllers/import_rule_controller.c`

- [ ] **Step 1: 下沉 `admin_controller.c` 的 SQL 至仓储与用例**
  - 在 `auth_repo_impl` 中提供 `mf_auth_repo_user_count(pool)` 和 `mf_auth_repo_create_admin(pool, ...)`。
  - `admin_controller.c:system_setup` 改为调用仓储/用例函数。
- [ ] **Step 2: 下沉 `receipt_controller.c` 的分类查询 SQL 至 `category_repo_impl`**
  - 使用已有的 `category_repo_impl` 查询首个支出分类和名称匹配分类。
- [ ] **Step 3: 下沉 `import_rule_controller.c` 的规则重置 SQL 至 `import_rule_repo_impl`**
  - 在 `import_rule_repo_impl` 中封装 `mf_import_rule_delete_all(pool, user_id)`。
- [ ] **Step 4: 隔离环境编译与全量单元测试验证**
  - `./backend/tests/run_isolated.sh build backend/build`
  - `./backend/tests/run_isolated.sh unit backend/build`
- [ ] **Step 5: 提交代码**
  - Git commit: `refactor(controllers): eliminate direct SQL queries and enforce DDD repository boundary`

---

### Task 6: 移动端未初始化路由引导优化及全量双引擎回归验证 (DEF-06 & Final Verification)

**Files:**
- Modify: `frontend/src/router/mobile.ts`
- Run: 全量回归测试脚本

- [ ] **Step 1: 优化移动端路由守卫未初始化逻辑**
  - 在 `frontend/src/router/mobile.ts` 中，当 `auth.isInitialized === false` 时，引导至移动端登录/初始化说明或 `/m/login` 时展示明确引导，避免死循环重定向。
- [ ] **Step 2: 前端测试与生产构建验证**
  - `npm --prefix frontend test`
  - `npm --prefix frontend run build`
- [ ] **Step 3: 全量隔离环境单元测试 (70 项)**
  - `./backend/tests/run_isolated.sh unit backend/build`
- [ ] **Step 4: 全量隔离环境端到端集成测试 (14 项)**
  - `./backend/tests/run_isolated.sh integration backend/build`
- [ ] **Step 5: 提交代码并交付**
  - Git commit: `fix(mobile): optimize uninitialized route guard and verify regression suites`
