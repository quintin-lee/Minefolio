# 多账本与家庭协同空间实现计划 (Multi-Ledger & Family Spaces Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 Minefolio 的多账本与家庭空间协同系统 (Multi-Ledger & Spaces)，支持多场景独立核算（个人/家庭/副业/投资等）、多用户协同记账（所有者 Owner / 记账者 Editor / 只读 Viewer 权限控制）、邀请码与用户名邀请加入，以及存量数据的平滑无缝迁移。

**Architecture:** 基于经典三层 C 架构，在数据层引入 `ledgers` 与 `ledger_members` 表，通过 `ctx_ledger_id` 上下文与 `X-Ledger-Id` 请求头实现无侵入权限隔离与自动默认账本回退；前端采用 Pinia `useLedgerStore` 全局状态驱动顶部 Header 切换器与成员管理弹窗。

**Tech Stack:** C23, SQLite/PostgreSQL, csilk HTTP, Vue 3, TypeScript, Pinia, Element Plus, Iconify.

---

## 任务拆解清单 (Task Breakdown)

### Task 1: 数据库迁移与模型层 (Database Migration & Model Layer)
- [ ] **Files to modify/create**:
  - `backend/sql/migration.sql`
  - `backend/sql/migration_postgres.sql`
  - `backend/src/common/db.c`
  - `backend/src/common/ledger_types.h`
- [ ] **Details**:
  1. 在 `migration.sql` 与 `migration_postgres.sql` 中新增 `ledgers` 与 `ledger_members` 表及索引。
  2. 在业务表 (`assets`, `transactions`, `daily_expenses`, `categories`, `dca_plans`, `cashflow_schedules`) 中新增 `ledger_id INTEGER` 关联列。
  3. 在 `backend/src/common/db.c` 的 `db_run_migrations` 中添加动态创建表与字段迁移逻辑，并为存量用户自动创建 `is_default=1` 的默认账本并回填历史数据。
  4. 创建 `backend/src/common/ledger_types.h` 定义 C 结构体。
- [ ] **Verification**: `cmake --build backend/build --parallel` 编译通过。

---

### Task 2: 账本与成员数据仓库 (Ledger & Member Repository)
- [ ] **Files to modify/create**:
  - `backend/src/repositories/ledger_repo.h`
  - `backend/src/repositories/ledger_repo.c`
- [ ] **Details**:
  1. 实现 `ledger_list_by_user`（查询用户拥有的和参与的所有账本及统计）。
  2. 实现 `ledger_create`（插入账本并自动插入 `owner` 成员记录）。
  3. 实现 `ledger_get`、`ledger_update`、`ledger_delete`（级联删除私有资产与流水）。
  4. 实现 `ledger_member_list`、`ledger_member_add`、`ledger_member_update_role`、`ledger_member_remove`。
  5. 实现 `ledger_generate_invite_code`、`ledger_find_by_invite_code`、`ledger_get_user_role`。
- [ ] **Verification**: 编译通过。

---

### Task 3: 业务仓库账本作用域适配 (Scoping Repositories by Ledger)
- [ ] **Files to modify/create**:
  - `backend/src/repositories/asset_repo.c`
  - `backend/src/repositories/transaction_write.c`
  - `backend/src/repositories/transaction_query.c`
  - `backend/src/repositories/daily_expense_query.c`
  - `backend/src/repositories/daily_expense_write.c`
  - `backend/src/repositories/dca_repo.c`
  - `backend/src/repositories/cashflow_repo.c`
- [ ] **Details**:
  1. 在列表查询与插入函数中兼容 `ledger_id`：查询过滤 `ledger_id = ?`（或 `(ledger_id = ? OR ledger_id IS NULL)`），写入时将 `ledger_id` 持久化至数据库。
- [ ] **Verification**: 编译通过。

---

### Task 4: 账本上下文鉴权与业务服务层 (Context Helper & Services)
- [ ] **Files to modify/create**:
  - `backend/src/common/ctx.h`
  - `backend/src/services/ledger_service.h`
  - `backend/src/services/ledger_service.c`
- [ ] **Details**:
  1. 在 `ctx.h` 中实现 `ctx_ledger_id(c, user_id, required_role)`：从 `X-Ledger-Id` 提取，校验成员身份与角色权限，支持自动回退到默认账本。
  2. 在 `ledger_service.c` 中实现：
     - `ledger_service_list`
     - `ledger_service_create`
     - `ledger_service_get`
     - `ledger_service_update`
     - `ledger_service_delete`
     - `ledger_service_list_members`
     - `ledger_service_add_member`
     - `ledger_service_update_member`
     - `ledger_service_remove_member`
     - `ledger_service_create_invite_code`
     - `ledger_service_join_by_invite`
- [ ] **Verification**: 编译通过。

---

### Task 5: HTTP 控制器与路由注册 (Controllers & Routes)
- [ ] **Files to modify/create**:
  - `backend/src/controllers/ledger_controller.h`
  - `backend/src/controllers/ledger_controller.c`
  - `backend/src/main.c`
- [ ] **Details**:
  1. 实现 `register_ledger_routes(app)`，注册 `/api/ledgers/*` 系列端点。
  2. 在 `main.c` 中引入并注册控制器路由。
- [ ] **Verification**: 编译通过。

---

### Task 6: 前端 API 客户端、类型与 Pinia 状态管理 (Frontend State & APIs)
- [ ] **Files to modify/create**:
  - `frontend/src/types/index.ts`
  - `frontend/src/api/ledgers.ts`
  - `frontend/src/stores/ledger.ts`
  - `frontend/src/utils/http.ts`
- [ ] **Details**:
  1. 在 `types/index.ts` 中定义 `Ledger`, `LedgerMember`, `LedgerInviteResult` 等接口。
  2. 创建 `api/ledgers.ts`。
  3. 创建 Pinia `useLedgerStore`：管理当前账本、账本列表、只读判定 `isViewer`。
  4. 在 `http.ts` 请求拦截器中自动附加 `X-Ledger-Id: currentLedger.id`。
- [ ] **Verification**: `npm --prefix frontend run build` 类型检查通过。

---

### Task 7: 前端账本切换器与成员协作组件 (Frontend UI & Layout Integration)
- [ ] **Files to modify/create**:
  - `frontend/src/components/LedgerSelector.vue` (顶部切换器)
  - `frontend/src/components/LedgerDialog.vue` (创建/编辑账本)
  - `frontend/src/components/LedgerMembersDialog.vue` (成员列表、权限管理、邀请码生成)
  - `frontend/src/views/Layout.vue` (集成顶部账本选择器)
- [ ] **Details**:
  1. 实现顶部 Header 下拉账本切换器，支持展示角色 Badge、一键切换、创建新账本、输入邀请码加入。
  2. 实现账本设置与成员管理弹窗，支持按用户名邀请与生成 6 位有时效邀请码。
- [ ] **Verification**: `npm --prefix frontend run build` 0 错误编译通过。

---

### Task 8: 端到端集成测试套件与验证 (Integration Test Suite & Verification)
- [ ] **Files to modify/create**:
  - `backend/tests/test_ledgers.sh`
- [ ] **Details**:
  1. 编写自动化集成测试脚本，覆盖：创建多账本 $\to$ 用户邀请 $\to$ 角色权限隔离 (`viewer` 写保护、`editor` 正常记账) $\to$ 邀请码加入 $\to$ 资产与流水隔离验证。
  2. 运行 `test_link.sh` 确保全部回归测试 100% 通过。
  3. 运行 `test_market_sync.sh` 与 `test_dca_cashflow.sh`。
- [ ] **Verification**: 全部集成测试 PASS=100%, FAIL=0。
