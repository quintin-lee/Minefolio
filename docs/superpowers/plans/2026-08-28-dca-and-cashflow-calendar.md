# 定投计划与股息/现金流日历实现计划 (DCA Plans & Cash Flow Calendar Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 Minefolio 的定投计划 (DCA Plans) 与股息/被动现金流预测日历 (Cashflow Calendar) 子系统，支持按周/月周期定投、智能止盈达标提醒、一键买入记账、历史与未来月度现金流日历可视化以及到期分红一键入账。

**Architecture:** 遵循经典三层 C 架构（Repositories $\to$ Services $\to$ Controllers），复用已有 `balance_apply_delta` 资金账户联动、`apply_position` 持仓计算与 `quote_engine` 实时行情；前端采用 Vue 3 + TypeScript，新增 `/plans` 页面（含定投列表、智能止盈徽标、月度现金流交互日历、仪表盘待办微件）。

**Tech Stack:** C23, SQLite/PostgreSQL, csilk HTTP, Vue 3, TypeScript, Pinia, Element Plus, ECharts, Iconify.

---

## 任务拆解清单 (Task Breakdown)

### Task 1: 数据库表结构迁移与模型定义
- [x] **Files to modify/create**:
  - `backend/sql/migration.sql`
  - `backend/sql/migration_postgres.sql`
  - `backend/src/common/db.c`
  - `backend/src/common/plan_types.h`
- [x] **Details**:
  1. 在 `migration.sql` 与 `migration_postgres.sql` 中新增 `dca_plans`、`dca_executions`、`cashflow_schedules` 三张表结构及索引。
  2. 在 `backend/src/common/db.c` 的 `db_run_migrations` 中添加动态创建表结构逻辑（兼容已运行数据库）。
  3. 创建 `backend/src/common/plan_types.h`，定义 C 结构体模型。
- [x] **Verification**: `cmake --build backend/build --parallel` 编译成功。

---

### Task 2: 定投计划与执行记录数据仓库 (DCA Repository)
- [x] **Files to modify/create**:
  - `backend/src/repositories/dca_repo.h`
  - `backend/src/repositories/dca_repo.c`
  - `backend/CMakeLists.txt`
- [x] **Details**:
  1. 实现 `dca_plan_create`、`dca_plan_list`（关联查询标的资产名称、代码、净值与资金账户名称）、`dca_plan_get`、`dca_plan_update`、`dca_plan_set_status`、`dca_plan_delete`。
  2. 实现 `dca_execution_create`、`dca_execution_list_by_plan`、`dca_execution_list_pending`、`dca_execution_get`、`dca_execution_update_status`。
  3. 在 `CMakeLists.txt` 中添加源文件并编译。
- [x] **Verification**: 编写简单单元断言或在构建目录编译通过。

---

### Task 3: 现金流计划与日历数据仓库 (Cashflow Repository)
- [x] **Files to modify/create**:
  - `backend/src/repositories/cashflow_repo.h`
  - `backend/src/repositories/cashflow_repo.c`
  - `backend/CMakeLists.txt`
- [x] **Details**:
  1. 实现 `cashflow_schedule_create`、`cashflow_schedule_list`、`cashflow_schedule_get`、`cashflow_schedule_update`、`cashflow_schedule_delete`。
  2. 实现 `cashflow_query_actual_transactions`（查询指定年月类型为 dividend/interest/income 的真实流水）。
  3. 在 `CMakeLists.txt` 中添加源文件。
- [x] **Verification**: 编译通过。

---

### Task 4: 定投计划与现金流业务服务层 (Services)
- [x] **Files to modify/create**:
  - `backend/src/services/dca_service.h`
  - `backend/src/services/dca_service.c`
  - `backend/src/services/cashflow_service.h`
  - `backend/src/services/cashflow_service.c`
  - `backend/CMakeLists.txt`
- [x] **Details**:
  1. `dca_service_confirm_execution`: 事务内生成买入交易记录，调用 `apply_position` 更新持仓与净值，调用 `balance_apply_delta` 扣减资金账户，更新 execution 状态为 `confirmed`。
  2. `dca_service_skip_execution`: 将 execution 状态更新为 `skipped`。
  3. `dca_service_list_plans`: 计算各计划累计定投金额、当前持仓市值、累计收益率，并比对 `target_profit_rate` 判定是否止盈达标。
  4. `cashflow_service_get_calendar`: 聚合历史真实流水 + 根据活跃计划规则按日推算当月预期到账事件，返回月度日历事件与汇总。
  5. `cashflow_service_confirm`: 一键生成分红/利息交易流水并更新收款账户余额。
- [x] **Verification**: 编译通过。

---

### Task 5: HTTP 控制器与路由注册 (Controllers)
- [x] **Files to modify/create**:
  - `backend/src/controllers/dca_controller.h`
  - `backend/src/controllers/dca_controller.c`
  - `backend/src/controllers/cashflow_controller.h`
  - `backend/src/controllers/cashflow_controller.c`
  - `backend/src/main.c`
- [x] **Details**:
  1. 实现 `/api/dca/plans` 系列 RESTful 路由及 `/api/dca/executions/:id/confirm`、`/api/dca/executions/:id/skip`。
  2. 实现 `/api/cashflow/schedules` 系列路由及 `/api/cashflow/calendar`、`/api/cashflow/confirm`。
  3. 在 `backend/src/main.c` 中注册路由。
- [x] **Verification**: 编译通过。

---

### Task 6: 后台调度器扩展 (Scheduler for DCA Executions)
- [x] **Files to modify/create**:
  - `backend/src/services/market/market_scheduler.c`
- [x] **Details**:
  1. 在后台调度线程中增加定投周期检查：每日早间 08:00 扫描所有活跃的 `dca_plans`。
  2. 若今天符合计划周期（按周几或按月几号）且当期尚无 execution，自动插入 `status='pending'` 待办记录。
- [x] **Verification**: 编译并运行无崩溃。

---

### Task 7: 前端 API 客户端与 TypeScript 类型定义
- [x] **Files to modify/create**:
  - `frontend/src/types/index.ts`
  - `frontend/src/api/dca.ts`
  - `frontend/src/api/cashflow.ts`
- [x] **Details**:
  1. 在 `types/index.ts` 中定义 `DcaPlan`, `DcaExecution`, `CashflowSchedule`, `CashflowCalendarEvent`, `MonthlyCashflowSummary` 等接口。
  2. 实现 `dcaApi` 与 `cashflowApi`。
- [x] **Verification**: `npm --prefix frontend run build` 类型检查通过。

---

### Task 8: 前端页面与交互组件开发
- [x] **Files to modify/create**:
  - `frontend/src/components/DcaPlanDialog.vue` (新增/编辑定投计划弹窗，支持选择标的与扣款账户)
  - `frontend/src/components/CashflowScheduleDialog.vue` (新增/编辑现金流计划弹窗)
  - `frontend/src/components/CashflowCalendar.vue` (月度日历网格、事件标签、一键确认到账抽屉)
  - `frontend/src/views/Plans.vue` (主页面：定投计划 Tab + 现金流日历 Tab + 待办提醒横幅)
  - `frontend/src/router/index.ts` (注册 `/plans` 路由与侧边栏菜单)
  - `frontend/src/views/Dashboard.vue` (添加定投待办与近期现金流速览卡片)
- [x] **Details**:
  1. 实现定投计划卡片与列表、止盈达标高亮提示、一键买入入账。
  2. 实现现金流交互日历、年化被动收入预测柱状图、到期一键确认到账。
  3. 首页仪表盘集成待办提醒。
- [x] **Verification**: `npm --prefix frontend run build` 0 错误编译通过。

---

### Task 9: 端到端系统集成测试与部署
- [x] **Files to modify/create**:
  - `backend/tests/test_dca_cashflow.sh`
- [x] **Details**:
  1. 编写自动化集成测试脚本，覆盖：创建定投计划 $\to$ 触发待办 $\to$ 一键确认扣款买入 $\to$ 检查持仓与资金账户变动 $\to$ 止盈判定 $\to$ 现金流日历生成 $\to$ 一键确认分红到账。
  2. 运行 `test_link.sh` 确保全部回归测试 100% 通过。
  3. 部署并验证容器运行状态。
- [x] **Verification**: `PASS=100%, FAIL=0`。
