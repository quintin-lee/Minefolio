# Holdings Page Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add a standalone 持仓管理 (Holdings) page: a new backend endpoint `GET /api/reports/holdings` aggregating per-asset floating/realized PnL, a Vue page with summary cards + donut chart + bar chart + holdings table, and integration tests H1–H6.

**Architecture:** Backend adds one handler `report_holdings` in `backend/src/reports.c` reusing `report_transaction_performance`'s exact PnL semantics (per-asset accumulators over date-ordered buy/sell/income transactions) plus a holdings-row query over investment assets. Frontend adds a route `/holdings` + menu entry + API client + two ECharts components + the `Holdings.vue` page. Tests append to the existing shell integration suite.

**Tech Stack:** C23 + csilk (SQLite), Vue 3 + TypeScript + Element Plus + ECharts v5, bash + curl + jq + sqlite3 integration tests.

---

## Design Decisions (locked in spec `docs/superpowers/specs/2026-08-14-holdings-page-design.md`)

- **PnL semantics = EXACT copy of `report_transaction_performance`** (per-asset): buy `cost_for_pnl += amount` (fee EXCLUDED), qty +=; sell `realized += amount − qty_sold×avg_cost` where `avg_cost = cost_for_pnl/qty` (cost_for_pnl NOT reduced on sell — mirrors performance quirk), qty −=; income `cost_for_pnl −= amount` **and** `realized += amount`.
- **`floating_pct` 除零防护**: `cost_basis == 0` → emit `0` (both per-row and summary). Never NaN in JSON.
- **Zero-position asset** (qty=0 but has trade history) still returned — realized_pnl may be nonzero.
- **Holdings rows** = `assets JOIN categories WHERE user_id=? AND asset_type IN ('stock','fund','bond','crypto')` — regardless of quantity.
- **Per-asset realized only accumulates transactions on assets present in holdings** (investment assets only). Summary totals = Σ over holdings rows.
- Transactions iteration must be `ORDER BY transaction_date ASC` (global order preserves per-asset chronological order within groups).
- Response: `{summary: {total_market_value, total_cost_basis, total_floating_pnl, total_realized_pnl, floating_pct}, holdings: [{asset_id, name, asset_type, currency, quantity, net_value, cost_basis, current_value, floating_pnl, floating_pct, realized_pnl}]}`.

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `backend/src/reports.c` | Modify | New `report_holdings(csilk_ctx_t* c)` handler (append after `report_transaction_performance`) |
| `backend/src/main.c` | Modify | Forward-declare `report_holdings` + register route `/api/reports/holdings` |
| `backend/tests/test_link.sh` | Modify | Append section 33 (H1–H6) before the final PASS/FAIL block |
| `frontend/src/api/reports.ts` | Modify | `HoldingsItem`/`HoldingsSummary`/`HoldingsReport` interfaces + `reportsApi.holdings()` |
| `frontend/src/components/HoldingsTypePie.vue` | Create | Donut chart: market value share by asset_type |
| `frontend/src/components/HoldingsCostBar.vue` | Create | Grouped bar chart: cost_basis vs current_value per asset |
| `frontend/src/views/Holdings.vue` | Create | The holdings page (cards + 2 charts + table) |
| `frontend/src/router/index.ts` | Modify | Add `/holdings` child route after `assets` |
| `frontend/src/views/Layout.vue` | Modify | Menu item (`TrendCharts` icon) + `pageTitle` map entry |
| `frontend/src/locales/zh-CN.ts` | Modify | `nav.holdings: '持仓'` |

---

## Chunk 1: Backend endpoint (`reports.c` + `main.c`)

### Task 1.1: Add `report_holdings` handler to `reports.c`

**Files:**
- Modify: `backend/src/reports.c` (append after `report_transaction_performance` function, ~line 449)

- [x] **Step 1: Append the handler**

Add at end of `backend/src/reports.c`:

```c
/* ----------------------------------------------------------------
 * GET /api/reports/holdings
 * 持仓报表：按投资类资产聚合浮动盈亏 + 已实现盈亏
 * ---------------------------------------------------------------- */
typedef struct {
    int64_t asset_id;
    double cost_for_pnl;
    double qty;
    double realized;
} holding_pnl_t;

static int64_t holding_find(holding_pnl_t* arr, size_t n, int64_t asset_id)
{
    for (size_t i = 0; i < n; i++) {
        if (arr[i].asset_id == asset_id) {
            return (int64_t)i;
        }
    }
    return -1;
}

void report_holdings(csilk_ctx_t* c)
{
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        respond_unauthorized(c);
        return;
    }
    csilk_db_pool_t* pool = db_get_pool();

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { uid_str, NULL };

    /* 持仓行：投资类资产（不论 quantity 是否为 0） */
    const char* hold_sql =
        "SELECT a.id AS asset_id, a.name, c.asset_type, a.currency, "
        "a.quantity, a.net_value, a.cost_basis, a.current_value "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.user_id = ? AND c.asset_type IN ('stock','fund','bond','crypto') "
        "ORDER BY a.id ASC";
    csilk_json_t* hold_rows = csilk_db_query_param_json(pool, hold_sql, params);
    if (!hold_rows) {
        respond_error(c, 500, "查询失败");
        return;
    }

    size_t hn = csilk_json_array_size(hold_rows);
    holding_pnl_t* accs = NULL;
    if (hn > 0) {
        accs = (holding_pnl_t*)calloc(hn, sizeof(holding_pnl_t));
        if (!accs) {
            respond_error(c, 500, "内存不足");
            csilk_json_free(hold_rows);
            return;
        }
        for (size_t i = 0; i < hn; i++) {
            csilk_json_t* row = csilk_json_array_get(hold_rows, i);
            accs[i].asset_id = (int64_t)db_get_num(row, "asset_id");
        }
    }

    /* 用户全部交易，按日期升序（全局序保持各资产内时序） */
    const char* tx_sql =
        "SELECT asset_id, transaction_type, quantity, amount "
        "FROM transactions WHERE user_id = ? ORDER BY transaction_date ASC";
    csilk_json_t* tx_rows = csilk_db_query_param_json(pool, tx_sql, params);
    if (tx_rows) {
        size_t tn = csilk_json_array_size(tx_rows);
        for (size_t i = 0; i < tn; i++) {
            csilk_json_t* t = csilk_json_array_get(tx_rows, i);
            const char* type = csilk_json_get_string(t, "transaction_type");
            double amt = db_get_num(t, "amount");
            double qty = db_get_num(t, "quantity");
            int64_t aid = (int64_t)db_get_num(t, "asset_id");
            if (!type) {
                continue;
            }
            if (strcmp(type, "buy") != 0 && strcmp(type, "sell") != 0 &&
                strcmp(type, "income") != 0) {
                continue;               /* fee 等行跳过，与 performance 一致 */
            }
            int64_t idx = holding_find(accs, hn, aid);
            if (idx < 0) {
                continue;               /* 非投资类资产的交易，不计入持仓报表 */
            }
            if (strcmp(type, "buy") == 0) {
                accs[idx].cost_for_pnl += amt;  /* 不含 fee，与 performance 口径一致 */
                accs[idx].qty += qty;
            } else if (strcmp(type, "sell") == 0) {
                double avg_cost = accs[idx].qty > 0 ? accs[idx].cost_for_pnl / accs[idx].qty : 0.0;
                accs[idx].realized += amt - qty * avg_cost;
                accs[idx].qty -= qty;
            } else { /* income */
                accs[idx].cost_for_pnl -= amt;
                accs[idx].realized += amt;
            }
        }
        csilk_json_free(tx_rows);
    }

    /* 组装响应 */
    csilk_json_t* holdings = csilk_json_array();
    double total_market = 0.0, total_cost = 0.0, total_floating = 0.0, total_realized = 0.0;

    for (size_t i = 0; i < hn; i++) {
        csilk_json_t* row = csilk_json_array_get(hold_rows, i);
        double quantity = db_get_num(row, "quantity");
        double net_value = db_get_num(row, "net_value");
        double cost_basis = db_get_num(row, "cost_basis");
        double current_value = db_get_num(row, "current_value");
        double floating = current_value - cost_basis;
        double pct = (cost_basis == 0.0) ? 0.0 : (floating / cost_basis) * 100.0;

        total_market += current_value;
        total_cost += cost_basis;
        total_floating += floating;
        total_realized += accs[i].realized;

        csilk_json_t* h = csilk_json_object();
        csilk_json_add_number(h, "asset_id", db_get_num(row, "asset_id"));
        csilk_json_add_string(h, "name", csilk_json_get_string(row, "name"));
        csilk_json_add_string(h, "asset_type", csilk_json_get_string(row, "asset_type"));
        csilk_json_add_string(h, "currency", csilk_json_get_string(row, "currency"));
        csilk_json_add_number(h, "quantity", quantity);
        csilk_json_add_number(h, "net_value", net_value);
        csilk_json_add_number(h, "cost_basis", cost_basis);
        csilk_json_add_number(h, "current_value", current_value);
        csilk_json_add_number(h, "floating_pnl", floating);
        csilk_json_add_number(h, "floating_pct", pct);
        csilk_json_add_number(h, "realized_pnl", accs[i].realized);
        csilk_json_array_append(holdings, h);
    }
    csilk_json_free(hold_rows);
    free(accs);

    double sum_pct = (total_cost == 0.0) ? 0.0 : (total_floating / total_cost) * 100.0;
    csilk_json_t* summary = csilk_json_object();
    csilk_json_add_number(summary, "total_market_value", total_market);
    csilk_json_add_number(summary, "total_cost_basis", total_cost);
    csilk_json_add_number(summary, "total_floating_pnl", total_floating);
    csilk_json_add_number(summary, "total_realized_pnl", total_realized);
    csilk_json_add_number(summary, "floating_pct", sum_pct);

    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_object(resp, "summary", summary);
    csilk_json_add_array(resp, "holdings", holdings);
    respond_ok(c, resp);
}
```

- [x] **Step 2: Verify includes present**

`reports.c` already includes `"common/response.h"`, `"common/db.h"`, `"common/jwt.h"`, `"csilk/csilk.h"`, `<stdio.h>`, `<stdlib.h>`, `<string.h>`. `calloc`/`free` need `<stdlib.h>` (present). No new includes required.

- [x] **Step 3: Build check**

Run: `cmake --build backend/build --parallel`
Expected: compiles clean (no errors, no warnings for new code).

### Task 1.2: Register route in `main.c`

**Files:**
- Modify: `backend/src/main.c` (forward declarations ~line 47; route registration ~line 256)

- [x] **Step 1: Forward-declare**

After the line `extern void report_transaction_performance(csilk_ctx_t* c);` add:

```c
extern void report_holdings(csilk_ctx_t* c);
```

- [x] **Step 2: Register route**

After the line `csilk_app_get(app, "/api/reports/transaction/performance", report_transaction_performance);` add:

```c
csilk_app_get(app, "/api/reports/holdings", report_holdings);
```

- [x] **Step 3: Build check**

Run: `cmake --build backend/build --parallel`
Expected: compiles clean.

---

## Chunk 2: Integration tests (H1–H6)

### Task 2.1: Append holdings tests to `test_link.sh`

**Files:**
- Modify: `backend/tests/test_link.sh` (insert BEFORE the final block `echo ""; echo "结果: PASS=$PASS FAIL=$FAIL"; [ "$FAIL" -eq 0 ] || exit 1`, i.e. after section 32 / T5)

- [x] **Step 1: Insert the test section**

At the insertion point, add the following block. It creates fresh fund assets so expected numbers are deterministic and independent of the T1–T5 state (XX基金 is left untouched until H5, which fully sells it for the zero-position case):

```bash
echo ""
echo "== 33. 持仓报表 =="

# H1: 空态 — 新空用户持仓为空，summary 全 0
# JWT 直接用开发默认密钥伪造（服务器由本脚本以 MINEFOLIO_DB_DSN 启动，未设置 MINEFOLIO_JWT_SECRET）
EMPTY_UID=$(sqlite3 "$DB" "INSERT INTO users (username, password) VALUES ('holdings_empty','x'); SELECT last_insert_rowid();")
EMPTY_TOKEN=$(node -e "
const crypto = require('crypto');
const secret = 'minefolio-dev-secret-change-in-production';
const h = Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url');
const p = Buffer.from(JSON.stringify({sub:$EMPTY_UID,iat:Math.floor(Date.now()/1000)})).toString('base64url');
const s = crypto.createHmac('sha256', secret).update(h+'.'+p).digest('base64url');
process.stdout.write(h+'.'+p+'.'+s);
")
H1=$(curl -s -H "Authorization: Bearer $EMPTY_TOKEN" "$BASE/reports/holdings")
H1_EMPTY=$(echo "$H1" | jq -r '.data.holdings | length')
H1_MARKET=$(echo "$H1" | jq -r '.data.summary.total_market_value | floor')
H1_PCT=$(echo "$H1" | jq -r '.data.summary.floating_pct | floor')
check "H1 空用户 holdings 为空数组" "0" "$H1_EMPTY"
check "H1 空用户 summary 总市值 0" "0" "$H1_MARKET"
check "H1 空用户 floating_pct 0（无 NaN）" "0" "$H1_PCT"

# 新建基金分类与资产供 H2-H4 使用（不复用 T 段的 XX基金，保证数值独立）
# 载荷严格对齐 T 段现有测试：category POST 带 currency，asset POST 带 current_value:0 无 type 字段
H2_CAT=$(curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/categories" \
  -d '{"name":"基金二号类","type":"asset","asset_type":"fund","currency":"CNY"}' | jq -r '.data.id')
H2_ASSET=$(curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金二号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" | jq -r '.data.id')

# H2: 建仓浮动盈亏 — 买 1000×2，PUT net_value=2.5 → floating=500, pct=25
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-01\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X PUT "$BASE/assets/$H2_ASSET" \
  -d '{"net_value":2.5}' > /dev/null
H2=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H2_ROW=$(echo "$H2" | jq -c '.data.holdings[] | select(.name=="基金二号")')
check "H2 建仓 floating_pnl=500" "500" "$(echo "$H2_ROW" | jq -r '.floating_pnl | floor')"
check "H2 建仓 floating_pct=25" "25" "$(echo "$H2_ROW" | jq -r '.floating_pct | floor')"
check "H2 建仓 current_value=2500" "2500" "$(echo "$H2_ROW" | jq -r '.current_value | floor')"
check "H2 建仓 realized_pnl=0" "0" "$(echo "$H2_ROW" | jq -r '.realized_pnl | floor')"

# H2b: fee 不含入盈利口径 — 买 1000×2 fee=1，卖 100×2.5 → realized = 250-100*2.0 = 50
H2B_ASSET=$(curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金三号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" | jq -r '.data.id')
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2B_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"fee\":1,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-02\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H2B_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"sell\",\"amount\":250,\"quantity\":100,\"price_per_unit\":2.5,\"currency\":\"CNY\",\"transaction_date\":\"2026-01-03\"}" > /dev/null
H2B=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H2B_ROW=$(echo "$H2B" | jq -c '.data.holdings[] | select(.name=="基金三号")')
check "H2b 带 fee 买入后 realized=50（avg_cost 不含 fee）" "50" "$(echo "$H2B_ROW" | jq -r '.realized_pnl | floor')"

# H3: 卖出已实现盈亏 — 买 1000×2，卖 400×3 → realized=400, quantity=600
H3_ASSET=$(curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/assets" \
  -d "{\"name\":\"基金四号\",\"category_id\":$H2_CAT,\"current_value\":0,\"currency\":\"CNY\"}" | jq -r '.data.id')
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"buy\",\"amount\":2000,\"quantity\":1000,\"price_per_unit\":2,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-01\"}" > /dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"sell\",\"amount\":1200,\"quantity\":400,\"price_per_unit\":3,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-02\"}" > /dev/null
H3=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H3_ROW=$(echo "$H3" | jq -c '.data.holdings[] | select(.name=="基金四号")')
check "H3 卖出 realized=400" "400" "$(echo "$H3_ROW" | jq -r '.realized_pnl | floor')"
check "H3 卖出后 quantity=600" "600" "$(echo "$H3_ROW" | jq -r '.quantity | floor')"

# H4: 多资产聚合 — 各行字段求和 == summary 各总数；持仓行数
H4=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H4_COUNT=$(echo "$H4" | jq -r '.data.holdings | length')
H4_SUM_MARKET=$(echo "$H4" | jq -r '[.data.holdings[].current_value] | add | floor')
H4_SUM_COST=$(echo "$H4" | jq -r '[.data.holdings[].cost_basis] | add | floor')
H4_SUM_FLOAT=$(echo "$H4" | jq -r '[.data.holdings[].floating_pnl] | add | floor')
H4_SUM_REAL=$(echo "$H4" | jq -r '[.data.holdings[].realized_pnl] | add | floor')
H4_MARKET=$(echo "$H4" | jq -r '.data.summary.total_market_value | floor')
H4_COST=$(echo "$H4" | jq -r '.data.summary.total_cost_basis | floor')
H4_FLOAT=$(echo "$H4" | jq -r '.data.summary.total_floating_pnl | floor')
H4_REAL=$(echo "$H4" | jq -r '.data.summary.total_realized_pnl | floor')
check "H4 持仓行数=4（基金二号/三号/四号/XX基金）" "4" "$H4_COUNT"
check "H4 summary.total_market_value == Σcurrent_value" "$H4_SUM_MARKET" "$H4_MARKET"
check "H4 summary.total_cost_basis == Σcost_basis" "$H4_SUM_COST" "$H4_COST"
check "H4 summary.total_floating_pnl == Σfloating_pnl" "$H4_SUM_FLOAT" "$H4_FLOAT"
check "H4 summary.total_realized_pnl == Σrealized_pnl" "$H4_SUM_REAL" "$H4_REAL"

# H5: 零持仓 — 全卖光 XX基金（T 段资产）：quantity=0 行仍返回，floating_pnl=0, floating_pct=0
# XX基金 状态: T1 买1000×2(08-14), T3 卖400×3(08-15), T5 买100×2 fee5(08-16) → qty=700, cost_for_pnl=2200, realized=-50(400-450)
# 注意: H5 卖出日期必须晚于 T5(08-16)，否则时序颠倒导致 avg_cost 计算错误
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$FUND_ID,\"linked_asset_id\":$WALLET_ID,\"category_id\":$FUND_CAT,\"transaction_type\":\"sell\",\"amount\":1750,\"quantity\":700,\"price_per_unit\":2.5,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-20\"}" > /dev/null
H5=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H5_ROW=$(echo "$H5" | jq -c --argjson aid "$FUND_ID" '.data.holdings[] | select(.asset_id==$aid)')
check "H5 零持仓行仍返回 quantity=0" "0" "$(echo "$H5_ROW" | jq -r '.quantity | floor')"
check "H5 零持仓 floating_pnl=0" "0" "$(echo "$H5_ROW" | jq -r '.floating_pnl | floor')"
check "H5 零持仓 floating_pct=0（无 NaN）" "0" "$(echo "$H5_ROW" | jq -r '.floating_pct | floor')"
check "H5 零持仓 realized=-50（复用 performance 口径）" "-50" "$(echo "$H5_ROW" | jq -r '.realized_pnl | floor')"

# H6: 分红 — 对基金四号做 income 100 → realized 由 400 变为 500
# income 类型 qty_independent，载荷含 linked/category/currency 以对齐其他交易载荷
curl -s -H "$AUTH" -H "Content-Type: application/json" -X POST "$BASE/transactions" \
  -d "{\"asset_id\":$H3_ASSET,\"linked_asset_id\":$WALLET_ID,\"category_id\":$H2_CAT,\"transaction_type\":\"income\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-02-03\"}" > /dev/null
H6=$(curl -s -H "$AUTH" "$BASE/reports/holdings")
H6_ROW=$(echo "$H6" | jq -c --argjson aid "$H3_ASSET" '.data.holdings[] | select(.asset_id==$aid)')
check "H6 分红后 realized=500（400+100）" "500" "$(echo "$H6_ROW" | jq -r '.realized_pnl | floor')"
```

- [x] **Step 2: Run the full suite**

Run: `bash backend/tests/test_link.sh`
Expected: ALL checks pass — previous 79+ PASS + new H1–H6 checks, `FAIL=0`, exit 0.

- [x] **Step 3: Sanity — endpoint shape**

Run (server started by the suite, or manually):
`curl -s -H "$AUTH" http://localhost:8080/api/reports/holdings | jq '.code, .data.summary, (.data.holdings|length)'`
Expected: `0`, summary object, holdings array length ≥ 1.

- [x] **Step 4: Commit**

```bash
git add backend/src/reports.c backend/src/main.c backend/tests/test_link.sh
git commit -m "feat: add holdings report endpoint with per-asset PnL aggregation"
```

---

## Chunk 3: Frontend API client

### Task 3.1: Extend `reports.ts`

**Files:**
- Modify: `frontend/src/api/reports.ts`

- [x] **Step 1: Add interfaces**

After the existing `TransactionPerformance` interface, add:

```ts
export interface HoldingsItem {
  asset_id: number
  name: string
  asset_type: string
  currency: string
  quantity: number
  net_value: number
  cost_basis: number
  current_value: number
  floating_pnl: number
  floating_pct: number
  realized_pnl: number
}

export interface HoldingsSummary {
  total_market_value: number
  total_cost_basis: number
  total_floating_pnl: number
  total_realized_pnl: number
  floating_pct: number
}

export interface HoldingsReport {
  summary: HoldingsSummary
  holdings: HoldingsItem[]
}
```

- [x] **Step 2: Add API method**

Inside `reportsApi`, after `assetBreakdown:` (or in the same object), add:

```ts
  holdings: () => http.get<HoldingsReport, HoldingsReport>('/reports/holdings'),
```

- [x] **Step 3: Type check**

Run: `npm --prefix frontend run build`
Expected: `vue-tsc -b` passes with 0 errors.

---

## Chunk 4: Chart components

### Task 4.1: `HoldingsTypePie.vue` (donut — market value by asset_type)

**Files:**
- Create: `frontend/src/components/HoldingsTypePie.vue`

- [x] **Step 1: Create the component**

Copy the lifecycle skeleton from `AssetBreakdownPie.vue` (echarts init in onMounted, resize listener + ResizeObserver, watch deep, dispose in onUnmounted). Raw `asset_type` is used for color lookup; localized label only for display:

```vue
<template>
  <div ref="chartRef" class="holdings-pie-chart" />
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

export interface HoldingsPieDatum {
  name: string
  value: number
}

const props = defineProps<{ data: HoldingsPieDatum[] }>()

const chartRef = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

const TYPE_LABELS: Record<string, string> = {
  stock: '股票',
  fund: '基金',
  bond: '债券',
  crypto: '加密货币',
}
const TYPE_COLORS: Record<string, string> = {
  stock: '#00d4ff',
  fund: '#34d399',
  bond: '#fbbf24',
  crypto: '#f87171',
}

function updateChart() {
  if (!chart) return
  chart.setOption({
    tooltip: {
      trigger: 'item',
      formatter: (p: any) => `${p.name}: ¥${p.value} (${p.percent}%)`,
      backgroundColor: 'rgba(15,23,42,0.95)',
      borderColor: '#334155',
      textStyle: { color: '#e2e8f0' },
    },
    legend: { bottom: 0, textStyle: { color: '#94a3b8' } },
    series: [
      {
        type: 'pie',
        radius: ['50%', '75%'],
        center: ['50%', '45%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 4, borderColor: 'transparent', borderWidth: 2 },
        label: { show: false },
        data: props.data.map((d) => ({
          name: TYPE_LABELS[d.name] ?? d.name,   // 仅展示用本地化
          value: d.value,
          itemStyle: { color: TYPE_COLORS[d.name] ?? '#94a3b8' },  // 颜色按原始 asset_type
        })),
      },
    ],
  })
}

function handleResize() {
  chart?.resize()
}

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
  window.addEventListener('resize', handleResize)
  resizeObserver = new ResizeObserver(handleResize)
  resizeObserver.observe(chartRef.value!)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
})

watch(() => props.data, updateChart, { deep: true })
</script>

<style scoped>
.holdings-pie-chart {
  width: 100%;
  height: 280px;
}
</style>
```

> NOTE: `TYPE_COLORS` is keyed by raw `asset_type` (`d.name`), NOT by the localized label — the `itemStyle.color` lookup uses `d.name` before renaming. Do not "fix" this to use `TYPE_LABELS` keys.

- [x] **Step 2: Build check**

Run: `npm --prefix frontend run build`
Expected: passes. No Element Plus components in this file, so `components.d.ts` is unaffected by it.

### Task 4.2: `HoldingsCostBar.vue` (grouped bar — cost vs market per asset)

**Files:**
- Create: `frontend/src/components/HoldingsCostBar.vue`

- [x] **Step 1: Create the component**

Same lifecycle skeleton. Props: `data: { name: string; cost_basis: number; current_value: number }[]`.

```vue
<template>
  <div ref="chartRef" class="holdings-bar-chart" />
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

export interface HoldingsBarDatum {
  name: string
  cost_basis: number
  current_value: number
}

const props = defineProps<{ data: HoldingsBarDatum[] }>()

const chartRef = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function updateChart() {
  if (!chart) return
  chart.setOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: 'rgba(15,23,42,0.95)',
      borderColor: '#334155',
      textStyle: { color: '#e2e8f0' },
    },
    legend: { bottom: 0, textStyle: { color: '#94a3b8' } },
    grid: { left: 16, right: 16, top: 32, bottom: 48, containLabel: true },
    xAxis: {
      type: 'category',
      data: props.data.map((d) => d.name),
      axisLabel: { color: '#94a3b8', interval: 0, rotate: props.data.length > 4 ? 30 : 0 },
    },
    yAxis: {
      type: 'value',
      axisLabel: { color: '#94a3b8' },
      splitLine: { lineStyle: { color: '#1e293b' } },
    },
    series: [
      {
        name: '成本',
        type: 'bar',
        data: props.data.map((d) => d.cost_basis),
        itemStyle: { color: '#475569' },
        barMaxWidth: 24,
      },
      {
        name: '市值',
        type: 'bar',
        data: props.data.map((d) => d.current_value),
        itemStyle: { color: '#00d4ff' },
        barMaxWidth: 24,
      },
    ],
  })
}

function handleResize() {
  chart?.resize()
}

onMounted(() => {
  chart = echarts.init(chartRef.value!)
  updateChart()
  window.addEventListener('resize', handleResize)
  resizeObserver = new ResizeObserver(handleResize)
  resizeObserver.observe(chartRef.value!)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
})

watch(() => props.data, updateChart, { deep: true })
</script>

<style scoped>
.holdings-bar-chart {
  width: 100%;
  height: 280px;
}
</style>
```

- [x] **Step 2: Build check**

Run: `npm --prefix frontend run build`
Expected: passes.

---

## Chunk 5: The page — `Holdings.vue`

### Task 5.1: Create the view

**Files:**
- Create: `frontend/src/views/Holdings.vue`

- [x] **Step 1: Create the page**

Structure (per AGENTS.md patterns: `.page-header` space-between with `.header-actions`, summary cards grid, charts in a card row, `.premium-table`, `income-text`/`expense-text` classes, `formatCurrency` Intl zh-CN CNY, try/catch onMounted, `el-empty` when no holdings):

```vue
<template>
  <div class="holdings-page">
    <div class="page-header">
      <h2>持仓管理</h2>
      <div class="header-actions">
        <el-button :icon="Refresh" circle @click="load" />
      </div>
    </div>

    <!-- 汇总卡片 -->
    <el-row :gutter="24">
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总市值</div>
          <div class="summary-value">{{ formatCurrency(report?.summary.total_market_value ?? 0) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总浮动盈亏</div>
          <div class="summary-value" :class="pnlClass(report?.summary.total_floating_pnl ?? 0)">
            {{ formatSigned(report?.summary.total_floating_pnl ?? 0) }}
            <span class="summary-sub">({{ (report?.summary.floating_pct ?? 0).toFixed(2) }}%)</span>
          </div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">总已实现盈亏</div>
          <div class="summary-value" :class="pnlClass(report?.summary.total_realized_pnl ?? 0)">
            {{ formatSigned(report?.summary.total_realized_pnl ?? 0) }}
          </div>
        </div>
      </el-col>
    </el-row>

    <!-- 图表 -->
    <el-row :gutter="24" class="chart-row">
      <el-col :span="10">
        <div class="chart-card">
          <div class="chart-title">资产配置</div>
          <HoldingsTypePie :data="typeShare" />
        </div>
      </el-col>
      <el-col :span="14">
        <div class="chart-card">
          <div class="chart-title">成本 vs 市值</div>
          <HoldingsCostBar :data="barData" />
        </div>
      </el-col>
    </el-row>

    <!-- 持仓表格 -->
    <div class="table-card">
      <el-table :data="report?.holdings ?? []" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header" empty-text="">
        <el-table-column label="名称" min-width="140">
          <template #default="{ row }">
            <span class="asset-name">{{ ASSET_ICONS[row.asset_type] ?? '📦' }} {{ row.name }}</span>
          </template>
        </el-table-column>
        <el-table-column label="类型" width="100">
          <template #default="{ row }">
            <el-tag size="small" :type="typeTag(row.asset_type)">{{ typeLabel(row.asset_type) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="份额" width="110" align="right">
          <template #default="{ row }">{{ row.quantity.toFixed(2) }}</template>
        </el-table-column>
        <el-table-column label="净值" width="110" align="right">
          <template #default="{ row }">{{ row.net_value.toFixed(4) }}</template>
        </el-table-column>
        <el-table-column label="成本" width="120" align="right">
          <template #default="{ row }">{{ formatCurrency(row.cost_basis) }}</template>
        </el-table-column>
        <el-table-column label="市值" width="120" align="right">
          <template #default="{ row }">{{ formatCurrency(row.current_value) }}</template>
        </el-table-column>
        <el-table-column label="浮动盈亏" width="120" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">{{ formatSigned(row.floating_pnl) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="盈亏率" width="100" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.floating_pnl)">{{ row.floating_pct.toFixed(2) }}%</span>
          </template>
        </el-table-column>
        <el-table-column label="已实现盈亏" width="120" align="right">
          <template #default="{ row }">
            <span :class="pnlClass(row.realized_pnl)">{{ formatSigned(row.realized_pnl) }}</span>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无持仓数据" :image-size="80" />
        </template>
      </el-table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { Refresh } from '@element-plus/icons-vue'
import { reportsApi, type HoldingsReport } from '@/api/reports'
import HoldingsTypePie from '@/components/HoldingsTypePie.vue'
import HoldingsCostBar from '@/components/HoldingsCostBar.vue'

const report = ref<HoldingsReport | null>(null)

const ASSET_ICONS: Record<string, string> = {
  stock: '📈',
  fund: '📊',
  bond: '📉',
  crypto: '🪙',
}
const TYPE_LABELS: Record<string, string> = {
  stock: '股票',
  fund: '基金',
  bond: '债券',
  crypto: '加密货币',
}
const TYPE_TAGS: Record<string, 'primary' | 'success' | 'warning' | 'danger'> = {
  stock: 'primary',
  fund: 'success',
  bond: 'warning',
  crypto: 'danger',
}

function typeLabel(t: string): string {
  return TYPE_LABELS[t] ?? t
}
function typeTag(t: string): 'primary' | 'success' | 'warning' | 'danger' {
  return TYPE_TAGS[t] ?? 'info'
}
function pnlClass(v: number): string {
  return v > 0 ? 'income-text' : v < 0 ? 'expense-text' : ''
}
function formatCurrency(v: number): string {
  return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v)
}
function formatSigned(v: number): string {
  const s = new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(Math.abs(v))
  return v > 0 ? `+${s}` : v < 0 ? `-${s}` : s
}

const typeShare = computed(() => {
  const map = new Map<string, number>()
  for (const h of report.value?.holdings ?? []) {
    map.set(h.asset_type, (map.get(h.asset_type) ?? 0) + h.current_value)
  }
  return [...map.entries()].map(([name, value]) => ({ name, value }))
})

const barData = computed(() =>
  (report.value?.holdings ?? []).map((h) => ({
    name: h.name,
    cost_basis: h.cost_basis,
    current_value: h.current_value,
  })),
)

async function load() {
  try {
    report.value = await reportsApi.holdings()
  } catch (e) {
    console.error('加载持仓数据失败', e)
  }
}

onMounted(() => {
  load()
})
</script>

<style scoped>
.holdings-page {
  padding: 24px;
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.summary-sub {
  font-size: 12px;
  color: var(--text-secondary, #94a3b8);
  margin-left: 4px;
}
.chart-row {
  flex: 0 0 auto;
}
.chart-card,
.table-card {
  background: var(--surface, #141a2e);
  border: 1px solid var(--border-color, #1f2a4a);
  border-radius: 12px;
  padding: 16px;
}
.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
  margin-bottom: 8px;
}
.table-card {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}
.asset-name {
  font-weight: 500;
}
</style>
```

- [x] **Step 2: Build check**

Run: `npm --prefix frontend run build`
Expected: `vue-tsc -b` passes; `components.d.ts` regenerates with `ElButton`, `ElTag`, `ElTable`, `ElTableColumn`, `ElEmpty` (verify build regenerated it — do NOT hand-edit).

> NOTE: kept NO `v-loading`/`ElLoading` — auto-imported directives are config-dependent and error-prone; the `el-empty` + card layout is the sufficient loading UX.

---

## Chunk 6: Routing & navigation wiring

### Task 6.1: Router

**Files:**
- Modify: `frontend/src/router/index.ts` (children array, line ~26)

- [x] **Step 1: Add route**

After the `assets` child entry (`{ path: 'assets', name: 'Assets', component: () => import('@/views/Assets.vue') }`), add:

```ts
  { path: 'holdings', name: 'Holdings', component: () => import('@/views/Holdings.vue') },
```

### Task 6.2: Layout menu + page title

**Files:**
- Modify: `frontend/src/views/Layout.vue`

- [x] **Step 1: Add menu item**

After the `/assets` `<el-menu-item>` (~line 13-16), insert:

```html
        <el-menu-item index="/holdings">
          <el-icon><TrendCharts /></el-icon>
          <span>{{ t('nav.holdings') }}</span>
        </el-menu-item>
```

- [x] **Step 2: Add page title**

In the `pageTitle` map (lines ~117-124), after `'/assets': '资产管理',` add:

```ts
    '/holdings': '持仓管理',
```

### Task 6.3: Locale

**Files:**
- Modify: `frontend/src/locales/zh-CN.ts`

- [x] **Step 1: Add nav entry**

In the `nav` object, after `assets: '资产',` add:

```ts
    holdings: '持仓',
```

- [x] **Step 2: Full frontend build**

Run: `npm --prefix frontend run build`
Expected: 0 errors (this regenerates `components.d.ts` including new icons/menu usage).

---

## Chunk 7: Full verification + commit

### Task 7.1: Verify the whole system

- [x] **Step 1: Full verification gate**

Run:
```bash
cmake --build backend/build --parallel && npm --prefix frontend run build && bash backend/tests/test_link.sh
```
Expected: backend builds, frontend builds (0 errors), all tests pass (`FAIL=0`, exit 0, PASS ≥ 95).

- [x] **Step 2: Manual smoke test of the new page**

Start backend (`cd backend && MINEFOLIO_DB_DSN=./data/mf_holdings_smoke.db ./build/minefolio`), run `npm --prefix frontend run dev`, open `http://localhost:5173/holdings` in a browser:
- Menu shows 持仓 under 资产管理, page title 持仓管理
- Empty state: `el-empty` 暂无持仓数据; cards show ¥0.00
- With data: cards show totals, donut shows 4-type breakdown, bar shows cost vs market, table shows red/green PnL
- Refresh button reloads without error

- [x] **Step 3: Commit**

```bash
git add frontend/src/api/reports.ts frontend/src/components/HoldingsTypePie.vue frontend/src/components/HoldingsCostBar.vue frontend/src/views/Holdings.vue frontend/src/router/index.ts frontend/src/views/Layout.vue frontend/src/locales/zh-CN.ts frontend/src/components.d.ts
git commit -m "feat: add holdings page with charts and PnL table"
```

---

## Verification & Risks

- **Verification gate** (run before declaring done): `cmake --build backend/build --parallel && npm --prefix frontend run build && bash backend/tests/test_link.sh`
- **Risk — H5 expected −50 relies on performance quirk** (cost_for_pnl NOT reduced on sell): if the implementer "fixes" the semantics, H5 −50 will fail. The guard is intentional: spec mandates exact reuse of `report_transaction_performance` semantics. Do NOT deviate.
- **Risk — jq float vs int**: `.data.summary.total_market_value | floor` expectations must be integers (all test amounts are integral). `.floating_pct | floor` on 25.0 → 25. H4 totals compare computed sums (floored) — safe.
- **Risk — `components.d.ts`**: run frontend build to regenerate; never hand-edit.
- **Risk — H1 forged JWT**: depends on dev default secret `minefolio-dev-secret-change-in-production` (server launched by suite without `MINEFOLIO_JWT_SECRET`). If the env var is ever set in the test environment, H1 breaks — document in the test comment.
- **Empty DB / all-zero summary**: guarded by `cost_basis == 0.0 → floating_pct = 0` (both row and summary).