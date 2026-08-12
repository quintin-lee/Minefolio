# Minefolio — 收支与资产动态关联设计文档

## 1. 概述

当前 Minefolio 的收支记录（`daily_expenses`）与资产（`assets`）完全脱钩：记账支出/收入不影响任何资产余额，`assets.current_value` 只能手工维护。交易记录（`transactions`）同样记录在案但不联动余额。

本需求让收支与资产建立**动态关联**：记账即自动增减关联资产的余额，所有变动写入审计日志可回溯。

**关联语义：**
- 记收入 → 关联资产余额 `+amount`
- 记支出 → 关联资产余额 `-amount`
- 负债（贷款/信用卡）方向反转：刷信用卡记支出 → 欠款额 `+amount`；还款记收入 → 欠款额 `-amount`

**技术栈：** 后端 csilk C + SQLite，前端 Vue 3 + Element Plus。

---

## 2. 需求决策记录（已与用户逐项确认）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | 关联语义 | 余额自动增减：收入 `+`、支出 `-` |
| 2 | 可关联范围 | 全部资产 + 负债（负债方向反转） |
| 3 | 必选性 | 每条收支必须选择资产才能保存 |
| 4 | 存量收支（daily_expenses） | 清空重建，`asset_id NOT NULL` 强制新 schema |
| 5 | 存量交易（transactions） | 同样清空重建 |
| 6 | 联动范围 | daily_expenses 与 transactions 两者都联动 |
| 7 | 余额不足 | 允许负数，不拦截 |
| 8 | 余额维护 | `current_value` 存储字段直接增减 + 审计日志（可回溯） |
| 9 | 实现方案 | 方案 A：后端显式联动 + 共享工具函数（否决 SQLite 触发器：拿不到登录 user_id、负债方向逻辑难调试） |
| 10 | 迁移机制 | 列存在性门控的一次性迁移（DELETE 只触发一次） |
| 11 | 转账交易 | `transfer_in`/`transfer_out` 不联动余额（走 transfers 功能） |
| 12 | 审计字段 | `delta` + `balance_after` + `source_type`/`source_id` |

---

## 3. 数据库变更

### 3.1 清空存量数据（一次性迁移）

```sql
DELETE FROM expense_tags;      -- 引用 daily_expenses，必须先删
DELETE FROM daily_expenses;
DELETE FROM transactions;
```

> ⚠️ 此语句**受列存在性门控**，绝不无条件进入 migration.sql（每次启动执行会导致永久清空新数据）。仅在旧库检测到 `daily_expenses` 缺少 `asset_id` 列时执行一次。

### 3.2 daily_expenses 表变更

```sql
ALTER TABLE daily_expenses ADD COLUMN asset_id INTEGER NOT NULL REFERENCES assets(id);
CREATE INDEX idx_daily_expenses_asset ON daily_expenses(asset_id);
```

- 全新数据库：`migration.sql` 中 `CREATE TABLE daily_expenses` 直接包含 `asset_id` 列
- 存量数据库：列存在性门控触发上述 ALTER（此时表已清空，可安全加 `NOT NULL` 无默认值列）

### 3.3 审计日志表（新）

```sql
CREATE TABLE IF NOT EXISTS asset_balance_logs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id      INTEGER NOT NULL REFERENCES assets(id),
    user_id       INTEGER NOT NULL REFERENCES users(id),
    delta         DECIMAL(18,2) NOT NULL,     -- 本次变动（已含负债方向反转）
    balance_after DECIMAL(18,2) NOT NULL,     -- 变动后余额快照
    source_type   TEXT NOT NULL,              -- 'daily_expense' | 'transaction'
    source_id     INTEGER NOT NULL,           -- 对应主记录 id
    note          TEXT,                       -- 冗余描述（便于追溯）
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_balance_logs_asset ON asset_balance_logs(asset_id, created_at);
```

**设计说明：**
- `balance_after` 存快照而非 `balance_before`：查询日志直接读该列即得"该笔操作后的余额"，无需回放。需要 before 可用 `LAG()` 窗口函数推导。
- `delta` 是**已含方向反转**后的实际增量，`assets.current_value` 直接执行 `current_value = current_value + delta`，无需每次判断资产类型。
- `source_type + source_id` 支持从记录反向定位审计条目。

---

## 4. 迁移机制（列存在性门控）

`db_run_migrations()` 扩展流程：

```
1. 执行 migration.sql（IF NOT EXISTS 幂等）
   - 全新库：daily_expenses 建表含 asset_id，asset_balance_logs 建表 + 索引
   - 存量库：CREATE TABLE IF NOT EXISTS 跳过已存在表
2. C 代码查询 PRAGMA table_info(daily_expenses)
   - 存在 asset_id 列 → 跳过（全新库或已迁移，安全）
   - 不存在 → 执行一次性迁移（C 代码内依次执行）：
     a. DELETE FROM expense_tags
     b. DELETE FROM daily_expenses
     c. DELETE FROM transactions
     d. ALTER TABLE daily_expenses ADD COLUMN asset_id INTEGER NOT NULL REFERENCES assets(id)
     e. CREATE INDEX idx_daily_expenses_asset ...
     f. CREATE TABLE asset_balance_logs ... + 索引
3. 之后的启动：列已存在 → 门控不触发 → 数据永不被清空
```

**优点：**
- DELETE 受列存在性门控，**只执行一次**，新数据安全
- 无版本表、无多余机制，C 代码约 20 行
- 与现有"ALTER 忽略失败"风格一致

---

## 5. 后端设计：balance 模块

### 5.1 新文件 `backend/src/common/balance.h` / `balance.c`

```c
#pragma once
#include "csilk/drivers/db.h"
#include <stdint.h>

/**
 * @brief 对资产余额应用增减，并写入审计日志。
 *
 * delta 为业务方向金额（收入为正、支出为负），函数内部根据资产类型
 * （负债方向反转）归一化后更新 current_value，并记录 balance_after 快照。
 *
 * @param pool        数据库连接池
 * @param asset_id    目标资产 id
 * @param user_id     操作者（审计 + 归属校验）
 * @param delta       业务方向金额（正=增加余额，负=减少余额）
 * @param source_type "daily_expense" 或 "transaction"
 * @param source_id   对应主记录 id
 * @param note        冗余描述（可为 NULL）
 * @return 0 成功；-1 资产不存在或不属于该用户；-2 数据库错误
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t asset_id, int64_t user_id, double delta,
                        const char* source_type, int64_t source_id,
                        const char* note);

/** @brief 判断资产类型是否为负债（方向反转）。1=普通资产，-1=负债。 */
int balance_direction(const char* asset_type);
```

### 5.2 方向反转逻辑

```c
int balance_direction(const char* asset_type) {
    // 负债：loan / credit_card / other_liability
    if (!asset_type) return 1;
    if (strcmp(asset_type, "loan") == 0 ||
        strcmp(asset_type, "credit_card") == 0 ||
        strcmp(asset_type, "other_liability") == 0) {
        return -1;
    }
    return 1;
}
```

**归一化规则**：实际写入余额的增量 = `delta × direction`。调用方始终传业务方向金额（收入 `+`、支出 `-`），无需感知资产类型。

**负债语义示例：**

| 场景 | 业务 delta | 方向系数 | 余额实际变化 |
|------|-----------|---------|-------------|
| 普通资产记收入 +500 | +500 | 1 | `+500` |
| 普通资产记支出 -300 | -300 | 1 | `-300` |
| 信用卡刷卡记支出 500 | -500 | -1 | `+500`（欠款增加） |
| 信用卡还款记收入 500 | +500 | -1 | `-500`（欠款减少） |

### 5.3 核心实现（balance_apply_delta）

```c
int balance_apply_delta(...) {
    // 1. 查询资产归属与类型（WHERE user_id 越权过滤）
    //    SELECT current_value, currency FROM assets
    //    WHERE id=? AND user_id=?          → 无行返回 -1

    // 2. 归一化 delta
    double signed_delta = delta * balance_direction(asset_type);

    // 3. 更新余额（原子操作，避免读改写竞态）
    //    UPDATE assets SET current_value = current_value + ?, updated_at = ...
    //    WHERE id=? AND user_id=?

    // 4. 读取变动后余额（balance_after）
    //    SELECT current_value FROM assets WHERE id=?

    // 5. 写审计日志
    //    INSERT INTO asset_balance_logs(asset_id, user_id, delta, balance_after,
    //                                   source_type, source_id, note)
    //    VALUES (?,?,?,?,?,?,?)
    return 0;
}
```

> 第 3 步使用 `current_value = current_value + ?` 原子 SQL，避免读改写竞态。

---

## 6. 后端调用点改造

### 6.1 daily_expenses.c

create / update / delete 三个 handler 全部改为事务包裹：

```
daily_expenses_create:
  BEGIN
    INSERT daily_expenses (含 asset_id)
    balance_apply_delta(asset_id, user_id, ±amount, 'daily_expense', new_id, note)
  COMMIT / 失败 ROLLBACK

daily_expenses_update:
  BEGIN
    SELECT 旧记录 (amount, expense_type, asset_id)
    UPDATE daily_expenses SET 全字段 (含 asset_id)
    balance_apply_delta(新asset_id, user_id, 新delta, 'daily_expense', id, note)
    balance_apply_delta(旧asset_id, user_id, -旧delta, 'daily_expense', id, note)
    -- 新旧 asset_id 相同则合并为一次：(新delta - 旧delta)
  COMMIT / 失败 ROLLBACK

daily_expenses_delete:
  BEGIN
    SELECT 旧记录
    DELETE daily_expenses
    balance_apply_delta(asset_id, user_id, -旧delta, 'daily_expense', id, note)
  COMMIT / 失败 ROLLBACK
```

**delta 计算**：`expense_type == 'income' ? +amount : -amount`

**update 的 asset_id 变更**：若用户把记录从资产 A 改关联到资产 B，需 A 回退旧 delta、B 应用新 delta。新旧 asset_id 相同时合并为一次差值调用。

### 6.2 transactions.c

create / update / delete 改造同上，delta 语义按 `transaction_type` 映射：

| transaction_type | delta | 说明 |
|------------------|-------|------|
| `deposit` | `+amount` | 存入 |
| `withdrawal` | `-amount` | 取出 |
| `income` | `+amount` | 收益 |
| `fee` / `loss` | `-amount` | 手续费 / 亏损 |
| `buy` / `sell` | `+/-amount`（amount 按 `quantity × price_per_unit` 折算，若未提供则用 amount 字段） | 持仓类交易 |
| `transfer_in` / `transfer_out` | **不联动** | 走 transfers 功能（已确认） |

---

## 7. 错误处理

| 场景 | 行为 |
|------|------|
| 资产不存在或不属于当前用户 | balance_apply_delta 返回 -1 → 整体 ROLLBACK → HTTP 400 "资产无效" |
| 任一环节失败 | ROLLBACK，主记录 + 余额 + 审计日志全部回滚（事务原子性） |
| 余额变为负数 | 允许，不拦截（已确认） |
| 事务 begin 失败 | 返回 HTTP 500，不执行任何写入 |
| 审计日志写入失败 | 视为整体失败，ROLLBACK（审计不可缺失） |

---

## 8. 前端变更

### 8.1 daily_expenses API 与类型

- `DailyExpense` 类型新增 `asset_id: number` 与 `asset_name?: string`
- 列表接口返回时 JOIN 出 `asset_name` 供展示

### 8.2 DailyExpenses.vue（收支页）

- 表单新增**资产选择器**（必选）：
  - 数据源：`assetsApi.list()` 全量资产 + 负债
  - 选项显示：资产名称 + 当前余额（`name（¥current_value）`）
  - 负债项可加"负债"角标或颜色区分
- 提交前校验：未选资产 → 阻止提交并提示"请选择关联资产"
- 列表/表格新增"关联资产"列（显示资产名）
- 编辑对话框：回显已有 asset_id

### 8.3 Transactions.vue（交易页）

- 交易记录已有 `asset_id`（create/update 已选择资产），**无需新增选择器**
- 编辑时提示用户：修改金额将同步调整资产余额（信息提示）

### 8.4 Assets.vue（资产页）

- 无需变更（`current_value` 展示已存在）

### 8.5 类型定义（src/types/index.ts）

```ts
export interface DailyExpense {
  id: number;
  user_id: number;
  category_id: number;
  asset_id: number;            // 新增：关联资产（必选）
  asset_name?: string;         // JOIN 展示用
  expense_type: ExpenseType;
  amount: number;
  currency: string;
  expense_date: string;
  note?: string;
  tags?: Tag[];
  category_name?: string;
  created_at: string;
  updated_at: string;
}
```

---

## 9. 测试计划

### 9.1 balance.c 单元测试

| 用例 | 期望 |
|------|------|
| 普通资产 direction | 返回 1 |
| 负债（loan/credit_card/other_liability）direction | 返回 -1 |
| delta 归一化：普通资产收入 +500 | 余额 +500 |
| delta 归一化：负债支出 500 | 余额 +500（欠款增加） |

### 9.2 集成测试

| 场景 | 期望 |
|------|------|
| 建资产（余额 10000）→ 记收入 500 | 余额 10500，审计 1 条 delta=+500 |
| 记支出 300 | 余额 10200，审计 1 条 delta=-300 |
| 更新收支（500→800） | 余额 10500（净 +300 差量），审计 2 条 |
| 删除收支 | 余额回退 300，审计记录保留 |
| 更新时切换关联资产 A→B | A 回退旧 delta，B 应用新 delta |
| 信用卡刷卡 500（支出） | 信用卡余额 +500 |
| 信用卡还款 500（收入） | 信用卡余额 -500 |
| 余额不足（余额 100，支出 200） | 允许，余额 -100 |
| 非法资产 id / 他人资产 | 返回错误，主记录 + 余额 + 审计均不落库（事务原子性） |

### 9.3 迁移测试

| 场景 | 期望 |
|------|------|
| 全新库首次启动 | daily_expenses 含 asset_id，asset_balance_logs 建好 |
| 存量库首次启动 | 数据清空，asset_id 列添加成功，审计表建好 |
| 存量库第二次启动 | 门控不触发，数据保留，不重复清空 |
| 迁移中途失败 | 门控在下次启动重新尝试（幂等） |

---

## 10. 风险与注意事项

- **数据永久删除**：迁移清空存量收支/交易记录，不可恢复。已在需求确认阶段获得用户明确同意。
- **transactions 的 buy/sell delta 折算**：`quantity × price_per_unit` 与 `amount` 字段可能不一致，需在实现时明确优先级（建议：有 quantity+price 用乘积，否则用 amount，并在 API 文档注明）。
- **审计日志只增不删**：删除收支记录时审计日志保留（历史事实），删除后 `source_id` 悬空属正常，用 `source_type` 区分。
- **多用户资产越权**：所有 SQL 均带 `user_id` 条件，balance_apply_delta 内二次校验。
