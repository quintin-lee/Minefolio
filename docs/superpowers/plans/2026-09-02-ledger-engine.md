# Minefolio P0-02: Ledger Engine 账本状态机重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建统一、完备、强类型的金融核心账本状态机（Ledger Engine），确立“Transaction 是金融事实唯一来源（Single Source of Truth）”，将资产余额、持仓数量、加权成本基础、实现盈亏及组合估值全面定义为由账本引擎派生的物化状态（Derived/Materialized State），并实现高可靠的状态重算机制（Rebuild Engine）与幂等性保障。

**Architecture:** 
1. 在 `backend/src/core/ledger/` 创建 `ledger_types.h`, `ledger_engine.h`, `ledger_engine.c`，依托 P0-01 的 Financial Core 定点数计算能力进行全类型记账。
2. 重构 `transaction_write.c`, `daily_expense_write.c`, `transfer_service.c`, `dca_service.c`, `asset_service.c` 等，将所有分散直接修改 `current_value`, `quantity`, `cost_basis`, `net_value` 的代码统一交由 Ledger Engine 托管。
3. 实现 `ledger_rebuild_position`, `ledger_rebuild_account`, `ledger_rebuild_portfolio`，确保任意时刻重算派生状态均满足：`original state == rebuild state`。
4. 提供 RESTful 重建运维接口与测试套件。

**Tech Stack:** C23, SQLite / PostgreSQL, csilk framework, 128-bit Financial Core (`decimal_t`, `money_t`, `quantity_t`, `price_t`, `rate_t`, `percentage_t`).

---

## Task Breakdown

### Task 1: 扫描并分析全部余额/持仓直接变动点与 Ownership 重新定义
- **Files to Modify/Create:**
  - Create: `docs/ledger-engine-ownership-audit.md`
- [ ] **Step 1:** 全面扫描 `backend/src/` 下所有 `UPDATE assets SET`, `apply_position`, `balance_apply_delta` 及其业务调用方。
- [ ] **Step 2:** 梳理出各业务场景对资产状态的读写语义，产出 Ownership 归属矩阵与状态流转规则。
- [ ] **Step 3:** Commit 归档审计文档。

### Task 2: 设计与实现 Ledger Engine 领域模型及纯数学计算函数
- **Files to Create:**
  - Create: `backend/src/core/ledger/ledger_types.h`
  - Create: `backend/src/core/ledger/ledger_engine.h`
  - Create: `backend/src/core/ledger/ledger_engine.c`
  - Create: `backend/tests/unit/test_ledger_math.c`
- [ ] **Step 1:** 编写 `test_ledger_math.c` 测试用例（买入加权成本计算、卖出等比成本扣减与实现盈亏计算、浮动盈亏计算、分红与手续费摊薄）。
- [ ] **Step 2:** 验证测试编译与失败。
- [ ] **Step 3:** 实现 `ledger_calc_buy_position`, `ledger_calc_sell_position`, `ledger_calc_unrealized_pnl`。
- [ ] **Step 4:** 运行单元测试验证 100% 通过。
- [ ] **Step 5:** Commit: `feat(core): ✨ implement pure mathematical calculations for ledger engine`.

### Task 3: 实现 Ledger Engine 数据库应用与逆向回滚核心（`ledger_apply_tx` / `ledger_reverse_tx`）
- **Files to Modify/Create:**
  - Modify: `backend/src/core/ledger/ledger_engine.h`
  - Modify: `backend/src/core/ledger/ledger_engine.c`
  - Create: `backend/tests/unit/test_ledger_engine.c`
- [ ] **Step 1:** 编写 `test_ledger_engine.c` 集成单元测试（验证买入建仓、追加买入、部分卖出、全部卖出、关联资金扣减、手续费子项级联、逆向回滚、幂等校验）。
- [ ] **Step 2:** 实现 `ledger_apply_tx`, `ledger_reverse_tx`, `ledger_apply_expense`, `ledger_reverse_expense`, `ledger_apply_transfer`, `ledger_reverse_transfer`。
- [ ] **Step 3:** 运行 `test_ledger_engine` 验证全流程通过。
- [ ] **Step 4:** Commit: `feat(core): ✨ implement ledger_apply_tx and ledger_reverse_tx with full atomicity`.

### Task 4: 实现 Ledger Rebuild 机制（`rebuild_position`, `rebuild_account`, `rebuild_portfolio`）
- **Files to Modify:**
  - Modify: `backend/src/core/ledger/ledger_engine.h`
  - Modify: `backend/src/core/ledger/ledger_engine.c`
  - Modify: `backend/tests/unit/test_ledger_engine.c`
- [ ] **Step 1:** 在 `test_ledger_engine.c` 中添加从空状态重放全部交易的 Rebuild 测试用例，验证 `original state == rebuild state`。
- [ ] **Step 2:** 实现 `ledger_rebuild_position`、`ledger_rebuild_account` 与 `ledger_rebuild_portfolio`。
- [ ] **Step 3:** 运行单元测试验证状态重建完全一致。
- [ ] **Step 4:** Commit: `feat(core): ✨ implement ledger rebuild engine and event-sourcing verification`.

### Task 5: 重构交易、日常收支、转账与定投服务全面接入 Ledger Engine
- **Files to Modify:**
  - Modify: `backend/src/services/transaction_write.c`
  - Modify: `backend/src/services/daily_expense_write.c`
  - Modify: `backend/src/services/transfer_service.c`
  - Modify: `backend/src/services/dca_service.c`
  - Modify: `backend/src/services/cashflow_service.c`
  - Modify: `backend/src/services/asset_service.c`
- [ ] **Step 1:** 将 `transaction_write.c` 的新增、更新、删除重构为调用 `ledger_apply_tx` / `ledger_reverse_tx`。
- [ ] **Step 2:** 将 `daily_expense_write.c` 与 `transfer_service.c` 接入 `ledger_apply_expense` 与 `ledger_apply_transfer`。
- [ ] **Step 3:** 将 `dca_service.c` 与 `cashflow_service.c` 接入 Ledger Engine。
- [ ] **Step 4:** 统一资产手动修改 `asset_service.c` 产生调整事实（Adjustment Transaction）或经由账本记录。
- [ ] **Step 5:** Commit: `refactor(services): ♻️ migrate transaction, expense, and transfer write services to ledger engine`.

### Task 6: 添加 Rebuild RESTful 运维接口与系统控制器
- **Files to Modify/Create:**
  - Modify: `backend/src/controllers/asset_controller.h`
  - Modify: `backend/src/controllers/asset_controller.c`
  - Modify: `backend/src/services/asset_service.h`
  - Modify: `backend/src/services/asset_service.c`
- [ ] **Step 1:** 新增 `POST /api/assets/rebuild` (支持 `asset_id` 或全量 `portfolio`) 接口。
- [ ] **Step 2:** 编写控制器路由与权限校验。
- [ ] **Step 3:** Commit: `feat(api): ✨ add asset and portfolio rebuild endpoints`.

### Task 7: 集成测试套件与 CMake/CTest 全量回归验证
- **Files to Modify:**
  - Modify: `backend/CMakeLists.txt`
  - Modify: `backend/tests/test_link.sh`
- [ ] **Step 1:** 将 `test_ledger_math` 与 `test_ledger_engine` 加入 CMake CTest 测试列表。
- [ ] **Step 2:** 在 `test_link.sh` 中增加 Rebuild 校验用例（在产生数十条交易后清空派生字段并调用 rebuild，校验恢复后的余额与持仓 100% 匹配）。
- [ ] **Step 3:** 运行全部 7 大集成测试套件验证。
- [ ] **Step 4:** 验证前端构建通过。
- [ ] **Step 5:** Commit: `test(ledger): ✅ integrate ledger engine tests into CTest and test_link.sh`.
