# 股票/基金交易优化 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan (no subagents per user constraint). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Assets 表增加持仓（quantity/cost_basis/net_value）三列，使 buy/sell 联动真实份额与成本，报表展示已实现/浮动盈亏，表单支持手续费便捷输入与按类型显示单位。

**Architecture:** assets 表加 3 列 → db.c 双分支条件迁移 → assets.c create/update/detail/list 支持 → transactions.c buy/sell 持仓重算差值 balance（投资资产不走 ±amount，走 delta=新市值−旧市值）→ fee 行同事务生成 → reports.c performance 重写返回新字段 → 前端 Assets.vue / Transactions.vue / Reports.vue 分别扩展。

**Tech Stack:** C23 + csilk + SQLite/PG + Vue 3 + TypeScript + Element Plus

**Spec:** [docs/superpowers/specs/2026-08-13-stock-fund-trading-design.md](../specs/2026-08-13-stock-fund-trading-design.md)

---

## Task 1: migration.sql + migration_postgres.sql 加 assets 三列

**Files:**
- Modify: `backend/sql/migration.sql:L28-39`
- Modify: `backend/sql/migration_postgres.sql:L28-39`

- [ ] **Step 1: 修改 migration.sql assets CREATE TABLE**

在 `current_value DECIMAL(18,2) DEFAULT 0` 之后加三列：

```sql
quantity         DECIMAL(18,4) NOT NULL DEFAULT 0,
cost_basis       DECIMAL(18,4) NOT NULL DEFAULT 0,
net_value        DECIMAL(18,4) NOT NULL DEFAULT 0,
```

完整 DDL（assets 表）：
```sql
CREATE TABLE IF NOT EXISTS assets (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id     INTEGER NOT NULL REFERENCES categories(id),
    name            TEXT NOT NULL,
    account_no      TEXT,
    current_value   DECIMAL(18,2) NOT NULL DEFAULT 0,
    quantity        DECIMAL(18,4) NOT NULL DEFAULT 0,
    cost_basis      DECIMAL(18,4) NOT NULL DEFAULT 0,
    net_value       DECIMAL(18,4) NOT NULL DEFAULT 0,
    currency        TEXT NOT NULL DEFAULT 'CNY',
    note            TEXT,
    created_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

- [ ] **Step 2: 同步修改 migration_postgres.sql**

postgres 使用 BIGSERIAL/BIGINT，列顺序一致。把同样的三列插入到 current_value 之后。

- [ ] **Step 3: 验证两个文件列数一致**

```bash
grep -A 20 "CREATE TABLE assets" backend/sql/migration.sql backend/sql/migration_postgres.sql
```

- [ ] **Step 4: 提交**

```bash
git add backend/sql/migration.sql backend/sql/migration_postgres.sql
git commit -m "feat(db): add quantity/cost_basis/net_value columns to assets table"
```

---

## Task 2: db.c 双分支条件迁移

**Files:**
- Modify: `backend/src/db.c:L235-L312`

- [ ] **Step 1: SQLite 分支追加 assets 迁移（L310 之后、free(sql) L312 之前）**

在 SQLite 条件迁移区（L235-312），txdir_schema 释放之后、free(sql) 之前，加 assets 3 列迁移：

```c
// Assets 持仓三列迁移（SQLite）
const char* asset_migrations[] = {
    "ALTER TABLE assets ADD COLUMN quantity DECIMAL(18,4) NOT NULL DEFAULT 0",
    "ALTER TABLE assets ADD COLUMN cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0",
    "ALTER TABLE assets ADD COLUMN net_value DECIMAL(18,4) NOT NULL DEFAULT 0",
    NULL
};
for (int i = 0; asset_migrations[i]; i++) {
    if (!col_exists(pool, "assets", i == 0 ? "quantity" : (i == 1 ? "cost_basis" : "net_value"))) {
        if (csilk_db_exec(pool, asset_migrations[i]) != 0) {
            fprintf(stderr, "assets migration failed: %s\n", asset_migrations[i]);
        }
    }
}
```

- [ ] **Step 2: PG 分支追加 assets 迁移（L116 后、return 0 前）**

在 PG 分支 migration_postgres.sql 执行后、return 0 前，仿 direction 先例：

```c
if (!col_exists(pool, "assets", "quantity")) {
    csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS quantity DECIMAL(18,4) NOT NULL DEFAULT 0");
    csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS cost_basis DECIMAL(18,4) NOT NULL DEFAULT 0");
    csilk_db_exec(pool, "ALTER TABLE assets ADD COLUMN IF NOT EXISTS net_value DECIMAL(18,4) NOT NULL DEFAULT 0");
}
```

- [ ] **Step 3: 重新 cmake configure（刷新 CMakeLists GLOB 不需要，本任务未新增 .c 文件）**

```bash
cmake --build build --parallel
```

- [ ] **Step 4: 提交**

```bash
git add backend/src/db.c
git commit -m "feat(db): migrate assets quantity/cost_basis/net_value columns (SQLite+PG)"
```

---

## Task 3: assets.c 支持新三列 + A2 净值重算

**Files:**
- Modify: `backend/src/assets.c`

- [ ] **Step 1: assets_list SELECT 加三列**

两处 SELECT（L36-38 和 L45-48）各加 `a.quantity, a.cost_basis, a.net_value`。

- [ ] **Step 2: assets_create INSERT 加三列**

INSERT 语句 L100-102 加 3 个参数（默认 0），VALUES 加 `NULLIF(?, '0'), NULLIF(?, '0'), NULLIF(?, '0')`。body 读 `db_get_num(body,"quantity")` 等，默认 0。

- [ ] **Step 3: assets_update 改 A2 语义**

原 L114-160 直接 UPDATE current_value。改：
1. 读旧记录 `SELECT current_value, quantity, net_value, asset_type FROM assets JOIN categories WHERE id=?`
2. 若 `asset_type IN ('stock','fund','bond','crypto')` 且 body 含 net_value：
   - `new_current = old_quantity * new_net_value`
   - `delta = new_current - old_current_value`
   - 调 `balance_apply_delta(pool, asset_id, user_id, delta, "asset_netvalue", asset_id, "net_value update")`
   - UPDATE SET net_value=?
3. 否则保持原逻辑（直接 UPDATE current_value）

- [ ] **Step 4: assets_detail SELECT 加三列，响应字段加三列**

L196-200 SELECT 加 `a.quantity, a.cost_basis, a.net_value`；L209-219 csilk_json_add_number 加三字段。

- [ ] **Step 5: 编译验证**

```bash
cmake --build build --parallel
```

- [ ] **Step 6: test_link.sh 跑基线确认无回归**

```bash
./tests/test_link.sh 2>&1 | tail -8
```

期望 `PASS=68 FAIL=0`（或 ≥68）。

- [ ] **Step 7: 提交**

```bash
git add backend/src/assets.c
git commit -m "feat(assets): support quantity/cost_basis/net_value with A2 net-value recompute"
```

---

## Task 4: transactions.c buy/sell 持仓联动 + 手续费 fee 行

**Files:**
- Modify: `backend/src/transactions.c`

- [ ] **Step 1: transactions_create 持仓联动插入点**

INSERT 成功后（L282 后）、tdelta 联动前（L285 前）插入持仓联动块：

```c
// 持仓联动（仅 buy/sell 且投资类资产）
double tdelta_balance = 0;
double new_cost_basis = 0;
int is_investment = 0;
if (tx_type_lookup(type)->is_trading) {
    // 读旧持仓
    csilk_json_t* holder = csilk_db_query_param_json(pool,
        "SELECT a.quantity, a.cost_basis, a.net_value, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id=c.id "
        "WHERE a.id=? AND a.user_id=?", params);
    if (!holder) { ... ROLLBACK ... }
    double old_qty = db_get_num(holder, "quantity");
    double old_cost = db_get_num(holder, "cost_basis");
    double old_net = db_get_num(holder, "net_value");
    const char* atype = csilk_json_get_string(holder, "asset_type");
    csilk_json_free(holder);
    is_investment = (atype && (strcmp(atype,"stock")==0 || strcmp(atype,"fund")==0 ||
                                       strcmp(atype,"bond")==0 || strcmp(atype,"crypto")==0));
    if (is_investment) {
        if (strcmp(type,"buy")==0) {
            // buy: quantity+=qty, cost_basis+=amount+fee, net_value=price_per_unit
            double new_qty = old_qty + qty;
            double fee_amount = fee > 0 ? fee : 0;
            double new_cost = old_cost + amount + fee_amount;
            double new_net = price;
            // delta = new_current - old_current = (new_qty * new_net) - old_current
            // 但 balance_apply_delta 需要 signed_delta（负债翻负已内置）
            double old_current = old_qty * old_net;  // 近似旧市值（可能不等 current_value）
            // 实际用 current_value 查更准，此处简化：用 old_qty*old_net 近似
            // 正确做法：直接读 current_value
            tdelta_balance = new_qty * new_net - old_current;
            // 更新 assets 表持仓
            csilk_db_exec(pool, "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=?",
                params_buy_holder, new_qty, new_cost, new_net, asset_id);
            // fee 行生成
            if (fee_amount > 0 && linked_asset_id > 0) {
                csilk_json_t* fee_params = csilk_json_object();
                csilk_json_object_add_number(fee_params, 1, user_id);
                csilk_json_object_add_number(fee_params, 2, linked_asset_id);
                csilk_json_object_add_number(fee_params, 3, 0.0); // linked=NULL
                csilk_json_object_add_number(fee_params, 4, category_id);
                csilk_json_object_add_string(fee_params, 5, "expense");
                csilk_json_object_add_string(fee_params, 6, "fee");
                csilk_json_object_add_string(fee_params, 7, "out");
                csilk_json_object_add_string(fee_params, 8, NULL);
                csilk_json_object_add_number(fee_params, 9, fee_amount);
                csilk_json_object_add_number(fee_params, 10, 0.0);
                csilk_json_object_add_number(fee_params, 11, 0.0);
                csilk_json_object_add_string(fee_params, 12, "CNY");
                csilk_json_object_add_string(fee_params, 13, transaction_date);
                csilk_json_object_add_string(fee_params, 14, "transaction fee");
                csilk_db_exec(pool,
                    "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, source_type, "
                    "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, "
                    "currency, transaction_date, note) VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    fee_params);
                csilk_json_free(fee_params);
                // fee 行直接 balance_apply_delta(linked, -fee)
                if (balance_apply_delta(pool, linked_asset_id, user_id, -fee_amount,
                        "transaction_fee", tx_id, "transaction fee") != 0) { ... }
                // 注意：fee 行不参与主交易的 tdelta（主交易 tdelta 已由上面的持仓重算承担）
                // 资金账户余额变化由 fee 行自己处理（已上面 balance_apply_delta）
                // 主交易的 ldelta 保持原有 tx_effective_ldelta 行为（对资金账户 ±amount）
            }
        } else if (strcmp(type,"sell")==0) {
            // sell: quantity-=qty, cost_basis-=qty*avg_cost, net_value 不变
            if (old_qty < qty) { /* ROLLBACK 报错 */ }
            double avg_cost = old_cost / old_qty;
            double new_qty = old_qty - qty;
            double new_cost = old_cost - qty * avg_cost;
            double new_net = old_net;
            double old_current = old_qty * old_net;
            tdelta_balance = new_qty * new_net - old_current;
            csilk_db_exec(pool, "UPDATE assets SET quantity=?, cost_basis=?, net_value=? WHERE id=?",
                params_sell_holder, new_qty, new_cost, new_net, asset_id);
            if (fee_amount > 0 && linked_asset_id > 0) {
                // 同 buy 生成 fee 行，但 realized -= fee
            }
        }
    }
}
```

**精简说明**：实际代码需要更细致地处理 param 数组构建（参看现有 L269-281）。关键点是：
1. INSERT 成功后读旧持仓（SELECT a.quantity, a.cost_basis, a.net_value, c.asset_type FROM assets JOIN categories WHERE id=?）
2. buy：new_qty=old_qty+qty，new_cost=old_cost+amount+fee，new_net=price，OLD_current≈old_qty*old_net（或直查 current_value），delta=new_qty*new_net - old_current，调 balance_apply_delta
3. sell：new_qty=old_qty-qty，avg_cost=old_cost/old_qty，new_cost=old_cost-qty*avg_cost，net_value 不变，delta=new_qty*new_net - old_current
4. fee>0 时同事务生成 fee 行（asset_id=linked_asset_id，金额=fee），调 balance_apply_delta(linked, -fee)

- [ ] **Step 2: 移除原 tdelta 对投资资产的 ±amount 调用**

原 L287-294 的 `balance_apply_delta(asset_id, tdelta)` 在 is_investment 且 tdelta 由持仓重算处理时跳过（设 tdelta=0）。

- [ ] **Step 3: 编译**

```bash
cmake --build build --parallel
```

- [ ] **Step 4: test_link.sh 基线**

```bash
./tests/test_link.sh 2>&1 | tail -8
```

- [ ] **Step 5: 提交**

```bash
git add backend/src/transactions.c
git commit -m "feat(transactions): position-linked balance for buy/sell with fee row generation"
```

---

## Task 5: transactions.c update/delete 持仓快照回滚

**Files:**
- Modify: `backend/src/transactions.c`

- [ ] **Step 1: transactions_update 持仓回滚**

原 L411-461 是 diff 联动（new_tdelta−old_tdelta）。投资类改为：
1. 读旧持仓快照（INSERT 前的 SELECT）
2. 按旧交易类型反向回滚：若旧是 buy → quantity−=old_qty，cost_basis−=old_amount，net_value 归零或不变
3. 按新交易类型正向前推：若新是 buy → quantity+=new_qty，cost_basis+=new_amount+new_fee，net_value=new_price

或者更简洁：**先按旧类型回滚持仓（反向调用持仓重算逻辑），再按新类型正向前推**。全在同一事务内。

- [ ] **Step 2: transactions_delete 持仓回滚**

原 L512-531 反转 tdelta。投资类改为：读旧持仓快照，按旧交易类型回滚持仓（同 update 的"旧类型反向"）。

- [ ] **Step 3: 编译 + test_link.sh**

```bash
cmake --build build --parallel
./tests/test_link.sh 2>&1 | tail -8
```

- [ ] **Step 4: 提交**

```bash
git add backend/src/transactions.c
git commit -m "feat(transactions): position rollback on update/delete for investment assets"
```

---

## Task 6: reports.c report_transaction_performance 重写

**Files:**
- Modify: `backend/src/reports.c:L338-L389`

- [ ] **Step 1: SELECT 加 asset 持仓字段**

原 L347-352 SELECT 加 `a.quantity, a.cost_basis, a.current_value`。

- [ ] **Step 2: 循环改为持仓盈亏上下文**

遍历结果集时：
- buy：`total_cost_basis_remaining += amount + fee`（累加成本）
- sell：计算 realized = amount - old_qty * avg_cost；累计 total_realized_pnl；从 total_cost_basis_remaining 中减去售出成本 `qty × avg_cost`
- 分红 income 投资类：cost_basis -= amount；realized_pnl += amount

额外聚合 `total_market_value` = Σ(current_value WHERE asset_type IN stock/fund/bond/crypto)。

- [ ] **Step 3: 返回结构扩展**

csilk_json_add_number 加：
- total_cost_basis_remaining
- total_market_value
- floating_pnl = total_market_value - total_cost_basis_remaining
- realized_pnl

保留 total_gain/total_loss/net_gain 兼容。

- [ ] **Step 4: trades[] 每笔加字段**

avg_cost_at_trade / realized / fee。

- [ ] **Step 5: 编译 + test_link.sh**

```bash
cmake --build build --parallel
./tests/test_link.sh 2>&1 | tail -8
```

- [ ] **Step 6: 提交**

```bash
git add backend/src/reports.c
git commit -m "feat(reports): rewrite transaction performance with position-based PnL"
```

---

## Task 7: 前端 types + reports.ts 扩展

**Files:**
- Modify: `frontend/src/types/index.ts:L15-L28`
- Modify: `frontend/src/api/reports.ts:L47-L62`

- [ ] **Step 1: Asset 接口加三字段**

```typescript
export interface Asset {
  id: number
  user_id: number
  category_id: number
  name: string
  account_no?: string
  current_value: number
  currency: string
  note?: string
  created_at: string
  updated_at: string
  category_name?: string
  asset_type?: string
  quantity?: number
  cost_basis?: number
  net_value?: number
}
```

- [ ] **Step 2: TransactionPerformance 接口扩展**

```typescript
export interface TransactionPerformance {
  total_trades: number
  total_gain: number
  total_loss: number
  net_gain: number
  total_cost_basis_remaining?: number
  total_market_value?: number
  floating_pnl?: number
  realized_pnl?: number
  trades: {
    id: number
    asset_name: string
    type: string
    date: string
    quantity: number
    price: number
    amount: number
    profit?: number
    avg_cost_at_trade?: number
    realized?: number
    fee?: number
  }[]
}
```

- [ ] **Step 3: 提交**

```bash
git add frontend/src/types/index.ts frontend/src/api/reports.ts
git commit -m "feat(frontend): extend Asset and TransactionPerformance types with position fields"
```

---

## Task 8: Assets.vue 表格+对话框扩展

**Files:**
- Modify: `frontend/src/views/Assets.vue`

- [ ] **Step 1: 表格加份额/成本/净值列（仅投资类）**

L53-57 的 current_value 列后加三列，`v-if="['stock','fund','bond','crypto'].includes(row.asset_type)"`。

- [ ] **Step 2: 对话框加持有份额/单位净值输入（仅投资类）**

L85-96 的当前价值/币种/备注后加条件显示输入。current_value 改为只读派生。

- [ ] **Step 3: form 加 quantity/net_value 字段**

L167 的 reactive form 加 `quantity: 0, net_value: 0`。openDialog 时从 asset 读取。handleSubmit 提交时带上。

- [ ] **Step 4: 编译前端**

```bash
npm --prefix frontend run build
```

- [ ] **Step 5: 提交**

```bash
git add frontend/src/views/Assets.vue
git commit -m "feat(ui): add position fields to assets dialog and table"
```

---

## Task 9: Transactions.vue 手续费输入 + 按类型单位

**Files:**
- Modify: `frontend/src/views/Transactions.vue`

- [ ] **Step 1: form 加 fee 字段**

L425-437 的 reactive form 加 `fee: 0`。

- [ ] **Step 2: 表单 buy/sell 区加手续费输入**

L252-280 的 trading-fields 区域内，单价×数量下方加 fee el-input-number（仅 buy/sell 显示）。

- [ ] **Step 3: 数量单位按 asset_type 显示**

新增 computed：
```typescript
const quantityUnit = computed(() => {
  const asset = assets.value.find(a => a.id === form.asset_id)
  if (!asset) return ''
  const map: Record<string, string> = { stock: '股', fund: '份', bond: '张', crypto: '币' }
  return map[asset.asset_type as string] || ''
})
```

标签改为「交易数量（{{ quantityUnit }}）」。

- [ ] **Step 4: handleSubmit 提交 fee**

form 已含 fee 字段，直接提交给后端（后端会生成 fee 行）。

- [ ] **Step 5: 编译前端**

```bash
npm --prefix frontend run build
```

- [ ] **Step 6: 提交**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(ui): add fee input and type-aware quantity units for buy/sell"
```

---

## Task 10: Reports.vue 交易表现卡片扩展

**Files:**
- Modify: `frontend/src/views/Reports.vue`

- [ ] **Step 1: 卡片模板扩展**

L86-111 的交易表现卡片，将原来的 4 格 perf-grid（总交易笔数/总收益/总亏损/净收益）扩展为包含新字段的布局。建议保留旧 4 格（兼容旧数据），下方新加 4 格：已实现盈亏 / 浮动盈亏 / 持仓市值 / 持仓成本。

- [ ] **Step 2: 计算字段**

利用 perf.realized_pnl / perf.floating_pnl / perf.total_market_value / perf.total_cost_basis_remaining。

- [ ] **Step 3: 编译前端**

```bash
npm --prefix frontend run build
```

- [ ] **Step 4: 提交**

```bash
git add frontend/src/views/Reports.vue
git commit -m "feat(ui): extend transaction performance card with position PnL fields"
```

---

## Task 11: test_link.sh 新增测试段 28+（7 场景）

**Files:**
- Modify: `backend/tests/test_link.sh`

- [ ] **Step 1: 准备分类与资产**

在测试段末尾（L306 前）插入新段。需要先创建一个 fund 类资产分类和两个资产（基金 + 资金账户）。

- [ ] **Step 2: T1 买入建仓**

```bash
# 买 1000 份 × 2 元 = 2000 元
curl ... POST /api/transactions {asset_id,FUND_ID,category_id,transaction_type:"buy",amount:2000,quantity:1000,price_per_unit:2,currency:"CNY",transaction_date:"2026-08-14"}
# 断言：sqlite3 "$DB" "SELECT quantity,cost_basis,net_value,current_value FROM assets WHERE id=$FUND_ID"
# 期望：1000.0000|2000.0000|2.0000|2000.0000
```

- [ ] **Step 2: T2 净值更新**

```bash
curl ... PUT /api/assets/$FUND_ID {net_value:2.5}
# 断言 current_value=2500
```

- [ ] **Step 3: T3 卖出+已实现盈亏**

```bash
curl ... POST /api/transactions {asset_id,FUND_ID,category_id,transaction_type:"sell",amount:1200,quantity:400,price_per_unit:3,...}
# 断言：quantity=600, cost_basis=1200 (2000-400*(2000/1000)), net_gain=400
```

- [ ] **Step 4: T4 浮动盈亏**

```bash
curl ... GET /api/reports/transaction/performance
# 断言 floating_pnl ≈ 300 (600*(2.5-2.0))，realized_pnl=400
```

- [ ] **Step 5: T5 手续费**

```bash
curl ... POST /api/transactions {asset_id,FUND_ID,category_id,transaction_type:"buy",amount:200,quantity:100,price_per_unit:2,fee:5,...}
# 断言：cost_basis=1205（1200+200+5），fee 行落库（type=fee）
```

- [ ] **Step 6: T6 编辑回滚**

```bash
curl ... PUT /api/transactions/$TX_ID {quantity:500}
# 断言持仓按差值重算
```

- [ ] **Step 7: T7 删除回滚**

```bash
curl ... DELETE /api/transactions/$TX_FEE_ID
curl ... DELETE /api/transactions/$TX_BUY_T5_ID
# 断言持仓归零
```

- [ ] **Step 8: 跑测试**

```bash
./tests/test_link.sh 2>&1 | tail -8
```

期望 `PASS=68`（或 68 + 新测试 PASS 数）`FAIL=0`。

- [ ] **Step 9: 提交**

```bash
git add backend/tests/test_link.sh
git commit -m "test: add stock/fund trading position tests (T1-T7)"
```

---

## Task 12: 浏览器端到端冒烟

**Files:** 无（验证 only）

- [ ] **Step 1: 启动测试后端**

```bash
setsid env MINEFOLIO_DB_DSN=/tmp/opencode/mf_stock_smoke.db ./build/minefolio </dev/null > /tmp/opencode/mf_stock.log 2>&1 &
```

- [ ] **Step 2: playwright 冒烟**

用 /tmp/opencode/pw/report_check.mjs 类似方式：注册资产→创建基金分类→买入→净值更新→卖出→查看报表→断言 UI 字段。

- [ ] **Step 3: 清理**

```bash
kill $(ps aux | grep "./minefolio" | grep -v grep | awk '{print $2}')
rm -f /tmp/opencode/mf_stock_smoke.db /tmp/opencode/mf_stock.log
```

- [ ] **Step 4: 无 commit**

---

## 验证门

全部 Task 完成后运行：

```bash
cmake --build build --parallel && npm --prefix frontend run build && cd backend && ./tests/test_link.sh
```

期望：
- cmake build 零错误
- npm build 零错误（warning about chunk size 可接受）
- test_link.sh PASS≥68 FAIL=0

## 环境约束

- 构建系统：Unix Makefiles（cmake -B build -DCMAKE_BUILD_TYPE=Debug 刷新 GLOB；cmake --build build --parallel 增量）
- test_link.sh 需 8080 空闲（脚本自启 server）
- 前端 build：npm --prefix frontend run build
- 用户 docker 进程勿动
- git 不 push
- 不用 subagents
