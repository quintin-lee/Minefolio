# 定投计划与股息/现金流日历系统设计文档 (DCA Plans & Cash Flow Calendar Design Spec)

## 1. 概述与目标

在个人投资与资产管理中，定投（Dollar Cost Averaging）和被动现金流（股息、利息、房租等）是实现长期复利和流动性管理的核心支柱。

本子系统的目标是为 Minefolio 构建一套高效、精准且体验闭环的**「定投计划与现金流日历」**能力：
1. **定投计划管理 (DCA Plans)**：支持用户设定按周、按双周、按月的固定投资计划，支持目标止盈率（如 +15%）自动监控与达标提醒。
2. **待办提醒与一键买入记账**：到达定投日后在控制台和计划页面生成待办事项，用户可确认最新行情净值并一键买入，自动生成交易记录、扣减资金账户并更新持仓份额。
3. **被动现金流计划与预测日历 (Cash Flow Calendar)**：支持为资产设定周期性被动收入规则（股票分红、债券付息、大额存单到期、租金等），以月度交互日历和未来12个月预测柱状图直观展示，支持到期一键确认为分红/利息入账。
4. **与现有系统的无缝集成**：复用刚刚上线的 `quote_engine` 实时行情与 `SymbolSelect` 标的选择组件，以及核心交易余额联动模块（`balance_apply_delta` / `apply_position`）。

---

## 2. 数据库模型设计 (Database Schema)

在 SQLite 与 PostgreSQL 中新增三张表：

### 2.1 定投计划表 `dca_plans`
```sql
CREATE TABLE IF NOT EXISTS dca_plans (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    target_asset_id BIGINT NOT NULL,
    funding_asset_id BIGINT NOT NULL,
    name VARCHAR(128) NOT NULL,
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly', -- 'weekly', 'biweekly', 'monthly'
    day_of_period INT NOT NULL DEFAULT 1,            -- 周几(1-7) 或 每月几号(1-31)
    amount DOUBLE PRECISION NOT NULL,                 -- 每期计划金额
    target_profit_rate DOUBLE PRECISION DEFAULT 0,    -- 目标止盈率(如 0.15 表示 15%, 0 为不设)
    target_total_amount DOUBLE PRECISION DEFAULT 0,   -- 计划总金额上限(0 为不设)
    target_total_periods INT DEFAULT 0,               -- 计划总期数上限(0 为不设)
    status VARCHAR(32) NOT NULL DEFAULT 'active',     -- 'active', 'paused', 'completed'
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_dca_plans_user_status ON dca_plans(user_id, status);
```

### 2.2 定投执行历史与待办表 `dca_executions`
```sql
CREATE TABLE IF NOT EXISTS dca_executions (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT NOT NULL,
    user_id BIGINT NOT NULL,
    period_date VARCHAR(16) NOT NULL,                -- YYYY-MM-DD
    planned_amount DOUBLE PRECISION NOT NULL,
    actual_amount DOUBLE PRECISION DEFAULT 0,
    executed_price DOUBLE PRECISION DEFAULT 0,
    executed_quantity DOUBLE PRECISION DEFAULT 0,
    transaction_id BIGINT DEFAULT NULL,              -- 关联真实 transactions.id
    status VARCHAR(32) NOT NULL DEFAULT 'pending',   -- 'pending', 'confirmed', 'skipped'
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_dca_exec_plan_period ON dca_executions(plan_id, period_date);
CREATE INDEX IF NOT EXISTS idx_dca_exec_user_pending ON dca_executions(user_id, status);
```

### 2.3 周期性现金流计划表 `cashflow_schedules`
```sql
CREATE TABLE IF NOT EXISTS cashflow_schedules (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    source_asset_id BIGINT NOT NULL,                 -- 产生收益的资产(股票/债券/房产)
    target_asset_id BIGINT NOT NULL,                 -- 收款资金账户(银行卡/钱包)
    name VARCHAR(128) NOT NULL,
    flow_type VARCHAR(32) NOT NULL DEFAULT 'dividend', -- 'dividend', 'interest', 'rent', 'maturity'
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly',  -- 'once', 'monthly', 'quarterly', 'semi_annual', 'annual'
    start_date VARCHAR(16) NOT NULL,                 -- YYYY-MM-DD
    end_date VARCHAR(16) DEFAULT '',                 -- 结束日期(可选)
    expected_amount DOUBLE PRECISION NOT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'active',    -- 'active', 'completed', 'cancelled'
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_user ON cashflow_schedules(user_id, status);
```

---

## 3. 后端架构与业务服务 (Backend Architecture)

按照 Minefolio 的经典三层 C 架构：

```
HTTP Layer:     backend/src/controllers/dca_controller.c/.h
                backend/src/controllers/cashflow_controller.c/.h
Business Layer: backend/src/services/dca_service.c/.h
                backend/src/services/cashflow_service.c/.h
Data Layer:     backend/src/repositories/dca_repo.c/.h
                backend/src/repositories/cashflow_repo.c/.h
Scheduler:      backend/src/services/market/market_scheduler.c (扩展定投扫描与止盈评估)
```

### 3.1 定投核心业务流
1. **待办自动生成**：
   - 每日 08:00，调度器扫描所有 `status='active'` 的 `dca_plans`。
   - 判定当日是否符合计划周期（如 `frequency='weekly'` 且 `day_of_period=4` 且今天是周四；或 `frequency='monthly'` 且 `day_of_period=10` 且今天是10号）。
   - 若符合且当期尚无 `dca_executions` 记录，自动插入一条 `status='pending'` 待办。
2. **一键确认定投买入 (`POST /api/dca/executions/:id/confirm`)**：
   - 入参：`actual_amount`（默认按 `planned_amount`）、`executed_price`（默认按标的最新 `net_value`）。
   - 计算份额：`executed_quantity = actual_amount / executed_price`。
   - 开启数据库事务：
     1. 调用 `transaction_repo` 插入一条 `type='buy'` 的交易记录（扣减资金账户，买入标的资产）。
     2. 调用 `apply_position()` 更新资产持仓份额、持仓成本和最新市值。
     3. 调用 `balance_apply_delta()` 扣减资金账户（银行卡）余额。
     4. 更新 `dca_executions` 的状态为 `confirmed`，记录 `transaction_id`。
   - 提交事务并返回最新状态。
3. **目标止盈评估**：
   - 在行情同步或夜间清算后，针对所有活跃定投计划，统计该计划累计已确认定投的金额 `total_invested`。
   - 读取标的当前最新市值 `current_value`，计算定投收益率 `profit_rate = (current_value - total_invested) / total_invested`。
   - 若 `target_profit_rate > 0` 且 `profit_rate >= target_profit_rate`，在计划列表标记 `profit_target_reached = true`。

### 3.2 现金流日历计算与确认
1. **月度日历聚合 (`GET /api/cashflow/calendar?year=2026&month=9`)**：
   - **历史实际发生**：查询该月已入账的真实交易（`type IN ('dividend', 'interest', 'income')`），标记为 `type='actual'`。
   - **未来预测发生**：根据所有活跃的 `cashflow_schedules`，按其 `frequency` 和 `start_date/end_date` 推算该月落在哪些日期，标记为 `type='projected'`。
2. **一键确认现金流到账 (`POST /api/cashflow/confirm`)**：
   - 入参：`schedule_id`, `date`, `amount`, `source_asset_id`, `target_asset_id`, `flow_type`。
   - 自动生成一条对应的交易流水（`dividend`、`interest` 或 `income`），资金计入 `target_asset_id`。

---

## 4. RESTful API 接口定义

### 4.1 定投接口 (DCA Endpoints)
- `GET /api/dca/plans`：获取用户的全部定投计划（包含累计定投额、当前持仓市值、累计收益率、止盈达成状态）。
- `POST /api/dca/plans`：创建定投计划。
- `GET /api/dca/plans/:id`：获取单个计划详情。
- `PUT /api/dca/plans/:id`：修改计划配置。
- `PUT /api/dca/plans/:id/status`：修改状态（`active` / `paused` / `completed`）。
- `DELETE /api/dca/plans/:id`：删除计划。
- `GET /api/dca/plans/:id/executions`：获取指定计划的历史每期执行明细。
- `GET /api/dca/executions/pending`：获取当前待执行的定投待办列表。
- `POST /api/dca/executions/:id/confirm`：确认执行当期定投买入。
- `POST /api/dca/executions/:id/skip`：跳过当期定投。

### 4.2 现金流日历接口 (Cash Flow Endpoints)
- `GET /api/cashflow/schedules`：获取所有周期性现金流计划。
- `POST /api/cashflow/schedules`：创建现金流计划。
- `PUT /api/cashflow/schedules/:id`：更新现金流计划。
- `DELETE /api/cashflow/schedules/:id`：删除现金流计划。
- `GET /api/cashflow/calendar`：查询指定年月的日历事件清单与月度汇总数据。
- `POST /api/cashflow/confirm`：确认现金流到账并生成交易入账。

---

## 5. 前端架构与用户体验 (Frontend Design)

### 5.1 页面与组件清单
1. **主页面**：`frontend/src/views/Plans.vue`
   - Tab 1: **定投计划 (DCA Plans)**
   - Tab 2: **现金流日历 (Cashflow Calendar)**
2. **API 模块**：
   - `frontend/src/api/dca.ts`
   - `frontend/src/api/cashflow.ts`
3. **TypeScript 类型**：`frontend/src/types/index.ts`
   - `DcaPlan`, `DcaExecution`, `CashflowSchedule`, `CalendarEvent`, `MonthlyCashflowSummary`
4. **组件支持**：
   - `frontend/src/components/DcaPlanDialog.vue`（新增/编辑定投弹窗，嵌入 `SymbolSelect`）
   - `frontend/src/components/CashflowScheduleDialog.vue`（新增/编辑现金流计划弹窗）
   - `frontend/src/components/CashflowCalendar.vue`（月度日历网格视图与事件标签交互）
5. **仪表盘微件 (Dashboard Widget)**：
   - 在 `frontend/src/views/Dashboard.vue` 增加「今日待办定投 & 近期被动现金流」快捷卡片。

---

## 6. 测试与质量保障 (Testing & Verification)

1. **单元与集成测试**：
   - 编写 `backend/tests/test_dca_cashflow.sh`：
     - 测试创建定投计划与生成待办；
     - 测试一键确认定投买入并校验资产持仓增加与资金账户扣减；
     - 测试达到止盈线时的状态判定；
     - 测试现金流计划生成日历预测与一键确认分红入账；
     - 校验数据库原子回滚与多并发安全性。
2. **回归测试**：运行 `backend/tests/test_link.sh` 确保全部 126 项断言继续 100% 通过。
3. **前端编译与类型检查**：运行 `npm --prefix frontend run build` 确保 TypeScript 0 错误。
