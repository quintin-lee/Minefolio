# 账本共享与多用户协同架构重构实现计划 (Ledger Sharing & Collaboration Refactor Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 彻底重构 Minefolio 账本共享与协同空间体系，消除仓储层和资金引擎中硬编码 `user_id` 的“用户孤岛”反模式，实现以 `ledger_id` 为安全边界的跨成员数据共享、公共资产记账联动、报表聚合、AI 上下文对齐以及移动端离线安全同步。

**Architecture:** 
1. **数据边界升级**：将日常记账、投资交易、分类体系、统计报表及资产查询的数据物理隔离边界从 `table.user_id = :caller_uid` 演进为 `table.ledger_id = :active_lid AND EXISTS (SELECT 1 FROM ledger_members WHERE ledger_id = :active_lid AND user_id = :caller_uid)`。
2. **资金引擎解耦**：改造 `balance_apply_delta_m_impl` 与资产存在性校验，以资产所属账本成员写权限（`editor`/`owner`）代替硬编码的所有者匹配，支持成员向共享资产记账并原子扣减余额。
3. **协同体验对齐**：统一分类与账户管理权限（允许 Editor 创建账户）、透传 AI 会话账本上下文、优化移动端 SQLite 本地差量清理逻辑防止跨账本误删。

**Tech Stack:** C23, SQLite/PostgreSQL (csilk db pool & nested savepoints), Vue 3, Pinia, TypeScript, bash integration tests.

**Spec:** 本计划实现对账本共享完整协同链路的重构与缺陷修复（见前序会话详细审查报告）。

## Global Constraints

- 保持 DDD 四层架构：Controller 不直接执行裸 SQL，所有 SQL 仅存在于 `infrastructure/repositories/*_repo_impl.c` 及核心引擎。
- 保证个人默认账本与历史单人模式 100% 零回归（当 `ledger_id <= 0` 或访问个人账本时，行为与现有完全一致）。
- 兼容 SQLite 与 PostgreSQL 双数据库方言（测试双引擎通过）。
- 所有涉及余额与资产变更的链路必须置于 `db_tx_scope_begin` / `db_tx_scope_commit` 事务/保存点范围内。
- 前后端接口字段格式与 REST 规范严格保持向后兼容。

## Review Focus

1. **跨成员公共资产记账**：成员 B 记录日常支出/交易扣减成员 A 创建的账本资产时，余额能否正确扣减且无 1002/1003 报错？
2. **账本全局视图完整性**：成员 B 录入账目后，Owner A 是否能立刻在流水列表、月度统计和分类报表中看到？
3. **多人资产列表可见性**：3 人以上协同场景下，非 Owner 成员与 Owner 是否均能看到账本内所有成员创建的资产？
4. **共享分类与账户创建**：成员 B 是否能继承账本分类并具有创建账户的权限？
5. **移动端离线切换安全**：在移动端切换账本执行同步时，本地 SQLite 是否会误删另一个账本的数据？

---

## Task Structure

### Task 1: 资金引擎 (Balance Engine) 与资产校验跨成员解耦

**Files:**
- Modify: `backend/src/common/balance.c:181-270`
- Modify: `backend/src/infrastructure/repositories/asset_repo_impl.c:807-820, 882-902`
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:304-317`
- Modify: `backend/src/application/transaction/usecases.c:60-70, 95-105`
- Modify: `backend/src/application/daily_expense/usecases.c:28-40`

**Details:**
1. 在 `asset_repo_impl.c` 和 `transaction_repo_impl.c` 中，将 `asset_exists` 与 `tx_asset_exists` 调整为支持账本成员校验：
   ```sql
   SELECT a.id FROM assets a
   WHERE a.id = ? AND (
       a.user_id = ?
       OR (a.ledger_id > 0 AND EXISTS (
           SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id = ?
       ))
   )
   ```
2. 在 `asset_repo_impl.c` 的 `asset_get_category_type` 中，支持从共享账本继承分类类型：
   ```sql
   SELECT c.asset_type FROM categories c
   WHERE c.id = ? AND (
       c.user_id = ?
       OR (c.ledger_id > 0 AND EXISTS (
           SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = c.ledger_id AND lm.user_id = ?
       ))
   )
   ```
3. 在 `backend/src/common/balance.c` 的 `balance_apply_delta_m_impl` 中：
   - 第一步查询资产时，查询 `a.current_value, a.currency, a.ledger_id, a.user_id AS owner_uid, c.asset_type`，条件为 `WHERE a.id = ?`；
   - 校验权限：如果 `a.user_id != caller_user_id`，查询 `SELECT role FROM ledger_members WHERE ledger_id = ? AND user_id = ?`；若角色不是 `owner` 或 `editor`，则拒绝更新并回滚；
   - 更新语句执行 `UPDATE assets SET current_value = current_value + ?, updated_at = CURRENT_TIMESTAMP WHERE id = ? RETURNING current_value`；
   - 写入 `asset_balance_logs` 审计日志时记录真实的调用者操作人 ID。

- [x] **Step 1: 编写资金引擎跨成员扣减单元/集成测试用例**
- [x] **Step 2: 改造 `asset_exists`、`tx_asset_exists` 与 `asset_get_category_type`**
- [x] **Step 3: 改造 `balance_apply_delta_m_impl` 权限校验与更新逻辑**
- [x] **Step 4: 编译并运行验证测试通过**

---

### Task 2: 日常记账 (Daily Expense) 仓储与报表共享重构

**Files:**
- Modify: `backend/src/infrastructure/repositories/daily_expense_repo_impl.c:35-100, 200-280, 320-430`
- Modify: `backend/src/services/report_expense_service.c:60-700`

**Details:**
1. 在 `daily_expense_repo_impl.c` 中：
   - `de_list`：当 `ledger_id > 0` 时，将 `WHERE de.user_id=? AND de.ledger_id=?` 改造为：
     ```sql
     WHERE de.ledger_id = ? AND EXISTS (SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = de.ledger_id AND lm.user_id = ?)
     ```
     在 SELECT 列表中联查 `u.username AS operator_username`（通过 `LEFT JOIN users u ON de.user_id = u.id`）；
   - `de_monthly_stats`、`de_category_stats`、`de_tag_stats`、`de_summary`：统一升级为基于 `ledger_id` 与成员鉴权聚合；
   - `de_get`、`de_update`、`de_delete`：支持操作人是记录创建者，或者操作人在该账本中拥有 `owner`/`editor` 权限。
2. 在 `report_expense_service.c` 中：
   - 6 个报表聚合函数（`overview`, `trend`, `compare`, `structure`, `calendar`, `tag_analysis`）中，当 `ledger_id > 0` 时，统一支持账本成员查询账本内全部支出流水，生成真实的家庭总览报表。

- [x] **Step 1: 编写多成员日常记账与统计可见性测试验证**
- [x] **Step 2: 修改 `daily_expense_repo_impl.c` 中的列表、统计与操作 SQL**
- [x] **Step 3: 修改 `report_expense_service.c` 中的支出报表聚合 SQL**
- [x] **Step 4: 编译运行现有测试与新增验证**

---

### Task 3: 交易 (Transactions)、分类 (Categories) 与资产账户协同

**Files:**
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:490-560, 680-745`
- Modify: `backend/src/infrastructure/repositories/category_repo_impl.c:15-60`
- Modify: `backend/src/infrastructure/repositories/asset_repo_impl.c:235-275`
- Modify: `backend/src/interfaces/http/controllers/asset_controller.c:45-56`
- Modify: `backend/src/interfaces/http/controllers/account_controller.c:80-90`

**Details:**
1. 在 `transaction_repo_impl.c` 中：
   - `tx_list` 和 `tx_monthly`：当 `ledger_id > 0` 时，支持账本成员查看该账本的所有交易流水，并在 SELECT 中联查 `u.username AS operator_username`；
   - `tx_update` / `tx_delete`：支持账本 `owner` 或记录创建者进行修改和删除。
2. 在 `category_repo_impl.c` 中：
   - `mf_category_repo_list`：当 `ledger_id > 0` 时，按 `c.ledger_id = ? AND EXISTS (SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = c.ledger_id AND lm.user_id = ?)` 查询，保证账本内所有成员共享同一套分类树。
3. 在 `asset_repo_impl.c` 与 `asset_controller.c` 中：
   - 废除脆弱的 `WHERE a.user_id IN (?,?)` 补丁，直接按账本和成员鉴权过滤：
     ```sql
     WHERE a.ledger_id = ? AND EXISTS (SELECT 1 FROM ledger_members lm WHERE lm.ledger_id = a.ledger_id AND lm.user_id = ?)
     ```
     配合已有的 RBAC `MF_RBAC_ASSET_VISIBLE_SQL` 规则，完美解决 3 人以上成员资产可见性及 Owner 反向失明问题。
4. 在 `account_controller.c` 中：
   - 将 `api_accounts_create` 的权限要求从 `"owner"` 放宽至 `"editor"`，允许协同记账成员在当前账本创建新账户容器。

- [x] **Step 1: 编写交易与分类共享测试**
- [x] **Step 2: 改造 `transaction_repo_impl.c` 交易列表与统计查询**
- [x] **Step 3: 改造 `category_repo_impl.c` 分类查询**
- [x] **Step 4: 改造 `asset_repo_impl.c` 资产列表查询与 `account_controller.c` 创建权限**
- [x] **Step 5: 验证编译通过**

---

### Task 4: AI 会话透传账本上下文与移动端离线同步安全修复

**Files:**
- Modify: `backend/src/interfaces/http/controllers/ai_controller.c`
- Modify: `backend/src/services/ai/tools/expense_tool.c:250-310`
- Modify: `backend/src/infrastructure/repositories/ai_repo_impl.c:134-185`
- Modify: `frontend/src/stores/sync.ts:355-375`

**Details:**
1. AI 账本上下文透传：
   - 在 `ai_controller.c` 中，从 `X-Ledger-Id` 或 query 提取 `ledger_id`，并注入 `ai_runtime_context_t->metadata`；
   - 在 `expense_tool.c` 中，AI 记账执行时优先读取上下文中的 `ledger_id` 并传入仓储插入函数；
   - 在 `ai_repo_impl.c` 中，若传入了合法的 `ledger_id > 0`，直接绑定该账本，不再盲目回退到用户的私有默认账本。
2. 移动端本地 SQLite 离线同步安全修复：
   - 在 `frontend/src/stores/sync.ts` 的 `pullRemote` 中，清理远端已删除行的逻辑增加账本隔离约束：
     ```sql
     SELECT id FROM ${table} WHERE __deleted = 0 AND (ledger_id = ? OR (ledger_id IS NULL AND ? = 0))
     ```
     仅清理属于当前拉取账本的陈旧行，杜绝切换账本时误删其他账本数据的严重 Bug。

- [x] **Step 1: 修改 `ai_controller.c` 与 AI tools 账本上下文**
- [x] **Step 2: 修改 `frontend/src/stores/sync.ts` 中的本地差量清理逻辑**
- [x] **Step 3: 运行前端构建 `npm --prefix frontend run build` 确保无编译错误**

---

### Task 5: 完整集成测试套件与全量端到端验证

**Files:**
- Modify: `backend/tests/test_ledgers.sh`
- Test: `backend/tests/test_link.sh`

**Details:**
1. 在 `test_ledgers.sh` 中增加端到端多成员协同记账测试场景：
   - Alice 创建家庭公共账本，Bob 加入（`editor` 角色）；
   - Alice 创建共享资产卡与分类；
   - Bob 成功列出 Alice 的共享分类与共享资产；
   - Bob 成功向 Alice 的共享资产发起日常记账（校验余额扣减正确，返回 0 成功）；
   - Alice 列表查询日常记账，成功查看到 Bob 的记账记录；
   - 验证月度支出统计与分类报表成功聚合 Alice 和 Bob 的数据；
   - Charlie 作为 `viewer` 加入，验证只读可见但记账拦截（1004 Forbidden）。
2. 执行全量验证：
   - 运行 `./backend/tests/test_ledgers.sh`；
   - 运行 `./backend/tests/test_link.sh`（40 个场景，确保 0 回归）；
   - 运行 `ctest --test-dir backend/build`。

- [x] **Step 1: 在 `test_ledgers.sh` 中追加协同端到端测试用例**
- [x] **Step 2: 执行 `test_ledgers.sh` 确保全部绿灯**
- [x] **Step 3: 执行 `test_link.sh` 验证全局零回归**
- [x] **Step 4: 运行 `ctest` 单元测试套件**
