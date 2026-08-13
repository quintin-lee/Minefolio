# Transaction Direction Data-Driven ("方向数据化") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make transaction statistics and display generic by persisting a cash-flow `direction` (+ linked-asset `linked_direction`) on every transaction row, driven by a single type-registry in C, so new transaction types can be added without touching any hardcoded `IN ('deposit','withdrawal',...)` lists scattered across 6+ files.

**Architecture:** Two axes are modeled separately. The **registry** (a `const tx_type_t TX_TYPES[]` table in `backend/src/transactions.c`) owns all per-type semantics: primary-asset balance direction, linked-asset direction, cash-flow/statistics direction, trading flag, and performance-report inclusion. On create/update/import the registry's stat direction is persisted into the new `direction` column and linked direction into `linked_direction`. All statistics (`transactions_monthly`, `report_transaction_performance`, `report_asset_summary`) and all frontend display logic then read the persisted `direction` column instead of hardcoded type lists. The `transaction_type` DB CHECK constraint is removed (registry is now the single source of truth; app layer validates unknown types → code 1002).

**Tech Stack:** C23 + csilk (SQLite + PostgreSQL dual support), SQL, Bash integration tests, Vue 3 / TypeScript.

**Registry mapping (already verified byte-for-byte identical to current `tx_delta`/`tx_linked_delta`/`transactions_monthly` behavior):**

| code | label | balance_dir | linked_dir | stat_dir | is_trading | in_performance |
|------|-------|-------------|------------|----------|------------|----------------|
| deposit | 存入 | in | out | in | 0 | 1 |
| withdrawal | 取出 | out | in | out | 0 | 1 |
| buy | 买入 | in | out | out | 1 | 1 |
| sell | 卖出 | out | in | in | 1 | 1 |
| transfer_in | 转入 | in | out | in | 0 | 0 |
| transfer_out | 转出 | out | in | out | 0 | 0 |
| fee | 手续费 | out | out | out | 0 | 1 |
| income | 收益 | in | in | in | 0 | 1 |
| loss | 亏损 | out | out | out | 0 | 1 |
| **interest (NEW)** | 利息 | in | out | in | 0 | 1 |

> Reasoning for explicit `linked_dir`: `income` is (in, in) and `fee`/`loss` are (out, out) — the linked direction is NOT a simple negation of the balance direction, so it must be stored explicitly in the registry. `balance_dir` and `stat_dir` differ only for `buy`/`sell`.

---

## File Structure

| File | Change |
|------|--------|
| `backend/sql/migration.sql` | transactions CREATE TABLE: add `direction`/`linked_direction` columns, remove `transaction_type` CHECK |
| `backend/sql/migration_postgres.sql` | same DDL change for PG |
| `backend/src/common/db.c` | SQLite conditional migrations (ALTER + backfill + CHECK-removal rebuild) + PG branch migrations |
| `backend/src/transactions.c` | `TX_TYPES` registry + `tx_type_lookup()`; rewrite `tx_delta`/`tx_linked_delta` via registry; create/update validation + persist directions; list returns directions; monthly stats by `direction` |
| `backend/src/reports.c` | `report_transaction_performance` gain/loss by `direction`; `report_asset_summary` 30d net by `direction` |
| `backend/src/import_export.c` | CSV import: validate type via registry, persist directions; export unchanged (type already encodes direction; adding a column would break round-trip column alignment) |
| `frontend/src/types/index.ts` | `Direction` type + `Transaction.direction`/`linked_direction` fields |
| `frontend/src/views/Transactions.vue` | single `TRANSACTION_TYPES` constant (label/direction/isTrading/tagType); all helpers read it |
| `backend/tests/test_link.sh` | update test 18 outflows expectation (500→600); add tests for interest, unknown type, import, backfill, monthly 2026-09 |

---

### Task 1: Update fresh-install DDL (migration.sql + migration_postgres.sql)

**Files:**
- Modify: `backend/sql/migration.sql:41-60`
- Modify: `backend/sql/migration_postgres.sql:41-60`

- [ ] **Step 1: Rewrite the transactions CREATE TABLE in `backend/sql/migration.sql`**

Replace lines 41-60 with (removes `transaction_type` CHECK, adds two direction columns):

```sql
CREATE TABLE IF NOT EXISTS transactions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset_id         INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    linked_asset_id  INTEGER REFERENCES assets(id) ON DELETE SET NULL,
    category_id      INTEGER REFERENCES categories(id) ON DELETE RESTRICT,
    source_type      TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),
    transaction_type TEXT NOT NULL,
    direction        TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral')),
    linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),
    amount           DECIMAL(18,2) NOT NULL,
    price_per_unit   DECIMAL(18,4),
    quantity         DECIMAL(18,4),
    currency         TEXT DEFAULT 'CNY',
    transaction_date TIMESTAMP NOT NULL,
    note             TEXT,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

- [ ] **Step 2: Apply the identical change to `backend/sql/migration_postgres.sql`**

Same column additions / CHECK removal (column list identical; `INTEGER` stays `INTEGER` there by existing convention — do NOT introduce BIGSERIAL changes).

- [ ] **Step 3: Verify both files**

Run: `grep -n "linked_direction\|transaction_type" backend/sql/migration.sql backend/sql/migration_postgres.sql`
Expected: each file shows `transaction_type TEXT NOT NULL` (no CHECK) and both `direction` + `linked_direction` lines.

- [ ] **Step 4: Commit**

```bash
git add backend/sql/migration.sql backend/sql/migration_postgres.sql
git commit -m "feat(db): add direction columns to transactions, drop transaction_type check"
```

---

### Task 2: SQLite conditional migration in db.c (existing databases)

**Files:**
- Modify: `backend/src/common/db.c:226-241` (append new migration blocks after the `source_type` block, before `free(sql); return 0;`)

- [ ] **Step 1: Add the direction-column ALTER blocks with backfill**

Append immediately after the `source_type` migration block (line 238) and before line 240 (`free(sql);`):

```c
    // ---- transactions direction / linked_direction 列迁移 ----
    if (!col_exists(pool, "transactions", "direction")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN direction TEXT NOT NULL DEFAULT 'out' "
                "CHECK(direction IN ('in','out','neutral'))") != 0) {
            fprintf(stderr, "Migration error: cannot add direction to transactions\n");
            free(sql);
            return -1;
        }
    }
    if (!col_exists(pool, "transactions", "linked_direction")) {
        if (csilk_db_exec(pool,
                "ALTER TABLE transactions ADD COLUMN linked_direction TEXT "
                "CHECK(linked_direction IN ('in','out','neutral'))") != 0) {
            fprintf(stderr, "Migration error: cannot add linked_direction to transactions\n");
            free(sql);
            return -1;
        }
    }
    // 回填：direction / linked_direction 按存量类型推断（DEFAULT 'out' 已覆盖 out 类）
    csilk_db_exec(pool, "UPDATE transactions SET direction='in' WHERE transaction_type IN ('deposit','sell','income','transfer_in')");
    csilk_db_exec(pool, "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN ('sell','withdrawal','income','transfer_out')");
```

- [ ] **Step 2: Add the CHECK-removal table rebuild (gated on old DDL)**

Append after the backfill UPDATEs (still before `free(sql); return 0;`):

```c
    // ---- transactions 表 transaction_type CHECK 移除重建（须在 direction 列迁移之后） ----
    csilk_json_t* txdir_schema = csilk_db_query_json(pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='transactions'");
    if (txdir_schema && csilk_json_array_size(txdir_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(txdir_schema, 0), "sql");
        if (sql_def && strstr(sql_def, "CHECK(transaction_type IN") && !strstr(sql_def, "linked_direction")) {
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(pool,
                "CREATE TABLE transactions_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,"
                "  linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL,"
                "  category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT,"
                "  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),"
                "  transaction_type TEXT NOT NULL,"
                "  direction TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral')),"
                "  linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),"
                "  amount DECIMAL(18,2) NOT NULL,"
                "  price_per_unit DECIMAL(18,4),"
                "  quantity DECIMAL(18,4),"
                "  currency TEXT DEFAULT 'CNY',"
                "  transaction_date TIMESTAMP NOT NULL,"
                "  note TEXT,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                ")");
            csilk_db_exec(pool,
                "INSERT INTO transactions_new (id, user_id, asset_id, linked_asset_id, category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, currency, "
                "transaction_date, note, created_at) "
                "SELECT id, user_id, asset_id, linked_asset_id, category_id, source_type, "
                "transaction_type, direction, linked_direction, amount, price_per_unit, quantity, currency, "
                "transaction_date, note, created_at FROM transactions");
            csilk_db_exec(pool, "DROP TABLE transactions");
            csilk_db_exec(pool, "ALTER TABLE transactions_new RENAME TO transactions");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (txdir_schema) csilk_json_free(txdir_schema);
```

> Both INSERT and SELECT use explicit column-name lists so column order cannot drift between intermediate rebuild states (the pre-existing `category_id` rebuild at line 174 creates a table WITHOUT `linked_asset_id`; the later ALTER re-adds it; by the time this final rebuild runs all columns exist). The `!strstr(sql_def, "linked_direction")` gate ensures the rebuild only fires for databases whose stored DDL predates the new columns.

- [ ] **Step 3: Verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: exit 0, no warnings.

- [ ] **Step 4: Manually verify upgrade path on a seeded old-database**

Run (from `backend/`):
```bash
rm -f /tmp/mf_upgrade_test.db
sqlite3 /tmp/mf_upgrade_test.db "CREATE TABLE transactions (
  id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE, linked_asset_id INTEGER,
  category_id INTEGER REFERENCES categories(id) ON DELETE RESTRICT,
  source_type TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income','expense')),
  transaction_type TEXT NOT NULL CHECK(transaction_type IN ('deposit','withdrawal','buy','sell','transfer_in','transfer_out','fee','income','loss')),
  amount DECIMAL(18,2) NOT NULL, price_per_unit DECIMAL(18,4), quantity DECIMAL(18,4),
  currency TEXT DEFAULT 'CNY', transaction_date TIMESTAMP NOT NULL, note TEXT,
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP);
INSERT INTO transactions (user_id, asset_id, transaction_type, amount, transaction_date) VALUES (1,1,'deposit',100,'2026-08-01'),(1,1,'fee',5,'2026-08-02');"
MINEFOLIO_DB_DSN=/tmp/mf_upgrade_test.db ./build/minefolio &
sleep 1; kill %1 2>/dev/null
sqlite3 /tmp/mf_upgrade_test.db "PRAGMA table_info(transactions); SELECT transaction_type, direction, linked_direction FROM transactions ORDER BY id;"
rm -f /tmp/mf_upgrade_test.db
```
Expected: table_info shows `direction` and `linked_direction`; rows read `deposit|in|out` and `fee|out|out`; no error output from the server; the rebuilt table DDL has no `CHECK(transaction_type IN`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/common/db.c
git commit -m "feat(db): migrate existing transactions with direction columns and drop type check (sqlite)"
```

---

### Task 3: PostgreSQL conditional migration in db.c

**Files:**
- Modify: `backend/src/common/db.c:87-111` (PG branch currently returns right after file exec — extend it)

- [ ] **Step 1: Extend the `g_is_postgres` branch**

Inside the `if (g_is_postgres)` block, after the existing `csilk_db_exec(pool, sql)` check and `free(sql)`, before `return 0;`, add:

```c
        // ---- 方向列 + 移除 transaction_type CHECK（PG 存量库） ----
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS direction TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral'))");
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN IF NOT EXISTS linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral'))");
        csilk_db_exec(pool, "UPDATE transactions SET direction='in' WHERE transaction_type IN ('deposit','sell','income','transfer_in')");
        csilk_db_exec(pool, "UPDATE transactions SET linked_direction='in' WHERE transaction_type IN ('sell','withdrawal','income','transfer_out')");
        csilk_db_exec(pool, "ALTER TABLE transactions DROP CONSTRAINT IF EXISTS transactions_transaction_type_check");
        return 0;
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add backend/src/common/db.c
git commit -m "feat(db): migrate transactions direction columns and drop type check (postgres)"
```

---

### Task 4: Shared type registry module (behavior-identical rewrite)

**Files:**
- Create: `backend/src/common/tx_types.h`
- Create: `backend/src/common/tx_types.c`
- Modify: `backend/src/transactions.c:1-48` (replace `tx_delta` / `tx_linked_delta`, keep `tx_effective_ldelta`)
- Note: `backend/CMakeLists.txt:22` already globs `src/common/*.c`, so the new `.c` is picked up automatically — but bare `file(GLOB)` is NOT re-scanned by `cmake --build`; re-run `cmake -B build -G Ninja` once after creating the file.

- [ ] **Step 1: Create the header `backend/src/common/tx_types.h`**

```c
#pragma once

typedef struct {
    const char* code;          /* transaction_type value */
    const char* label;         /* 中文标签（展示/前端） */
    const char* balance_dir;   /* 目标资产余额方向: in=+amount, out=-amount */
    const char* linked_dir;    /* 关联资金账户余额方向 */
    const char* stat_dir;      /* 现金流统计/展示方向: in/out（写入 direction 列） */
    int is_trading;            /* buy/sell：才显示单价×数量 */
    int in_performance;        /* 是否计入交易盈亏报表（语义上排除转账） */
} tx_type_t;

const tx_type_t* tx_type_lookup(const char* type);
```

- [ ] **Step 2: Create the implementation `backend/src/common/tx_types.c`**

```c
#include "tx_types.h"
#include <string.h>

static const tx_type_t TX_TYPES[] = {
    { "deposit",      "存入",   "in",  "out", "in",  0, 1 },
    { "withdrawal",   "取出",   "out", "in",  "out", 0, 1 },
    { "buy",          "买入",   "in",  "out", "out", 1, 1 },
    { "sell",         "卖出",   "out", "in",  "in",  1, 1 },
    { "transfer_in",  "转入",   "in",  "out", "in",  0, 0 },
    { "transfer_out", "转出",   "out", "in",  "out", 0, 0 },
    { "fee",          "手续费", "out", "out", "out", 0, 1 },
    { "income",       "收益",   "in",  "in",  "in",  0, 1 },
    { "loss",         "亏损",   "out", "out", "out", 0, 1 },
    { "interest",     "利息",   "in",  "out", "in",  0, 1 },  /* 新增类型：验证注册表驱动 */
};

const tx_type_t* tx_type_lookup(const char* type) {
    if (!type) return NULL;
    for (size_t i = 0; i < sizeof(TX_TYPES) / sizeof(TX_TYPES[0]); i++) {
        if (strcmp(TX_TYPES[i].code, type) == 0) return &TX_TYPES[i];
    }
    return NULL;
}
```

- [ ] **Step 3: Rewrite `tx_delta` and `tx_linked_delta` in `transactions.c` via the registry**

Remove the old hardcoded versions (lines 10-37) and add `#include "common/tx_types.h"` to the include block (lines 1-5). Replace with:

```c
static double tx_delta(const char* type, double amount, double price, double qty) {
    (void)price; (void)qty;
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) return 0;
    return strcmp(t->balance_dir, "in") == 0 ? amount : -amount;
}

static double tx_linked_delta(const char* type, double amount) {
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) return 0;
    return strcmp(t->linked_dir, "in") == 0 ? amount : -amount;
}
```

`tx_effective_ldelta` (lines 42-48) stays untouched.

- [ ] **Step 4: Rebuild backend (registers the new source file)**

Run: `cmake -B backend/build -G Ninja && cmake --build backend/build --parallel`
Expected: exit 0. (Bare `file(GLOB)` requires re-running configure once to pick up `tx_types.c`.)

- [ ] **Step 5: Verify behavior identical to old code**

Compare the registry table against the deleted hardcoded version: every one of the 9 legacy types must produce the same sign for both `tx_delta` and `tx_linked_delta`.

- [ ] **Step 6: Run existing test suite to prove zero regression**

Run: `cd backend && ./tests/test_link.sh`
Expected: PASS=55 FAIL=0 (all 55 current checks, including balance linkage tests 11, 12, 14).

- [ ] **Step 7: Commit**

```bash
git add backend/src/common/tx_types.h backend/src/common/tx_types.c backend/src/transactions.c
git commit -m "refactor(backend): extract shared tx type registry, drive deltas from it"
```

---

### Task 5: create/update persist directions + validate unknown types

**Files:**
- Modify: `backend/src/transactions.c` (`transactions_create` lines 193-320, `transactions_update` lines 322-464)

- [ ] **Step 1: Create — look up type, reject unknown with 1002**

In `transactions_create`, after the `type` extraction (line 204) and before the existing required-fields check (line 210), add:

```c
    const tx_type_t* ttype = tx_type_lookup(type);
    if (!ttype) {
        csilk_json_free(body);
        respond_bad_request(c, "未知交易类型");
        return;
    }
```

- [ ] **Step 2: Create — write direction columns in the INSERT**

Replace lines 266-280 with:

```c
    const char* ins_params[] = {
        uid_str, ast_str, last_str, cat_str, src_type, type,
        ttype->stat_dir, ttype->linked_dir,
        amt_str, price_str, qty_str, currency, date, note ? note : "", NULL
    };

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* ins = csilk_db_query_param_json(pool,
        "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, source_type, transaction_type, "
        "direction, linked_direction, "
        "amount, price_per_unit, quantity, currency, transaction_date, note) "
        "VALUES (?, ?, NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id", ins_params);
```

The remaining create logic (balance deltas via `tx_delta`/`tx_effective_ldelta`, COMMIT) is unchanged. `ttype` was already fetched in Step 1 and is in scope for the whole function body.

- [ ] **Step 3: Update — validate type + persist direction columns**

In `transactions_update`, after type extraction (line 370) add:

```c
    const tx_type_t* ntype = tx_type_lookup(type ? type : "");
    if (!ntype) {
        csilk_json_free(body);
        respond_bad_request(c, "未知交易类型");
        return;
    }
```

Then replace the UPDATE statement (lines 401-404) and its params (lines 387-392) with:

```c
    const char* up_params[] = {
        type ? type : "", ntype->stat_dir, ntype->linked_dir,
        amt_str, price_str, qty_str,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        cat_str, src_type ? src_type : "expense", last_str,
        id_str, uid_str, NULL
    };

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* up_res = csilk_db_query_param_json(pool,
        "UPDATE transactions SET transaction_type=?, direction=?, linked_direction=?, amount=?, price_per_unit=?, "
        "quantity=?, currency=?, transaction_date=?, note=?, "
        "category_id=?, source_type=?, linked_asset_id=NULLIF(?, '0') WHERE id=? AND user_id=?", up_params);
    if (up_res) csilk_json_free(up_res);
```

- [ ] **Step 4: Build + run existing suite**

Run: `cmake --build backend/build --parallel && cd backend && ./tests/test_link.sh`
Expected: build exit 0; PASS=55 FAIL=0.

- [ ] **Step 5: Commit**

```bash
git add backend/src/transactions.c
git commit -m "feat(backend): persist tx direction on create/update, reject unknown types with 1002"
```

---

### Task 6: list returns direction columns

**Files:**
- Modify: `backend/src/transactions.c:68-77`

- [ ] **Step 1: Add the two columns to the list SELECT**

Replace line 70-72 with:

```c
        "SELECT t.id, t.asset_id, t.linked_asset_id, t.category_id, t.transaction_type, t.source_type, "
        "t.direction, t.linked_direction, t.amount, "
        "t.price_per_unit, t.quantity, t.currency, t.transaction_date, t.note, "
```

- [ ] **Step 2: Build + smoke-test the list shape**

Run: `cmake --build backend/build --parallel && cd backend && ./tests/test_link.sh`
Expected: build exit 0; PASS=55 FAIL=0 (existing tests don't assert absence of fields).

- [ ] **Step 3: Commit**

```bash
git add backend/src/transactions.c
git commit -m "feat(backend): return direction columns in transactions list"
```

---

### Task 7: monthly stats grouped by direction (+ update test 18)

**Files:**
- Modify: `backend/src/transactions.c:165-173`
- Modify: `backend/tests/test_link.sh:211-215`

- [ ] **Step 1: Rewrite the monthly SQL**

Replace lines 165-173 with:

```c
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "SELECT "
        "  COALESCE(SUM(amount), 0) AS total_volume, "
        "  COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END), 0) AS inflows, "
        "  COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END), 0) AS outflows, "
        "  COUNT(*) AS count "
        "FROM transactions WHERE user_id=? AND transaction_date LIKE ?", params);
```

- [ ] **Step 2: Update test 18 expectations (deliberate behavior change)**

The transfer_out created at test 12 now counts as outflow (its `stat_dir` is `out`). Replace line 214:

```bash
check "transactions/monthly outflows=600" "600" "$(echo "$MONTH_RES" | jq -r '.data.outflows | floor')"
```

(Test DB state for month 2026-08 at that point: deposit 1000 → inflow; transfer_out 100 → outflow; buy 500 → outflow. `total_volume=1600`, `inflows=1000`, `outflows=600`, `count=3`.)

- [ ] **Step 3: Run suite — expect the ONE updated check green**

Run: `cmake --build backend/build --parallel && cd backend && ./tests/test_link.sh`
Expected: PASS=55 FAIL=0 (outflows now 600).

- [ ] **Step 4: Commit**

```bash
git add backend/src/transactions.c backend/tests/test_link.sh
git commit -m "feat(backend): group monthly stats by direction column, transfers now counted"
```

---

### Task 8: reports.c — performance & asset summary by direction

**Files:**
- Modify: `backend/src/reports.c:347-367` (performance), `backend/src/reports.c:423-426` (asset summary)

- [ ] **Step 1: Performance — select and classify by `direction`**

Replace lines 347-352 with:

```c
    csilk_json_t* result = csilk_db_query_param_json(pool,
        "SELECT t.id, a.name as asset_name, t.transaction_type, t.direction, t.transaction_date, "
        "t.quantity, t.price_per_unit, t.amount "
        "FROM transactions t JOIN assets a ON t.asset_id=a.id "
        "WHERE t.user_id=? AND t.transaction_type NOT IN ('transfer_in', 'transfer_out') "
        "ORDER BY t.transaction_date DESC", params);
```

Replace line 363 (the hardcoded white-list) with a direction check:

```c
        const char* dir = csilk_json_get_string(row, "direction");
        if (dir && strcmp(dir, "in") == 0) {
            total_gain += amt;
        } else {
            total_loss += amt;
        }
```

Keep the `WHERE ... NOT IN ('transfer_in','transfer_out')` exclusion — that is the single remaining enumerated point, and it expresses performance-report semantics (transfers are neither gain nor loss), not type mechanics.

- [ ] **Step 2: Asset summary 30d — direction-based net change (fixes gross-sum bug)**

Replace lines 423-426 with:

```c
    csilk_json_t* change_result = csilk_db_query_param_json(pool,
        "SELECT COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE -amount END),0) as net_change FROM transactions "
        "WHERE user_id=? AND transaction_date >= date('now','-30 days')", params);
```

> This fixes the pre-existing bug where `SUM(amount)` summed raw positive amounts (gross, not net). Transfers are now included but net-neutral per user across assets.

- [ ] **Step 3: Build + run suite**

Run: `cmake --build backend/build --parallel && cd backend && ./tests/test_link.sh`
Expected: build exit 0; PASS=55 FAIL=0. (No current test asserts 30d/performance numbers, but confirm nothing breaks.)

- [ ] **Step 4: Commit**

```bash
git add backend/src/reports.c
git commit -m "feat(backend): classify performance and 30d change by direction column"
```

---

### Task 9: CSV import — registry validation + direction persistence

**Files:**
- Modify: `backend/src/import_export.c:306-321`

- [ ] **Step 1: Validate type and derive directions in the import row loop**

After the `linked_id` lookup block (line 289) and before the amount parsing (line 291), add:

```c
        const tx_type_t* ttype = tx_type_lookup(tx_type_s);
        if (!ttype) {
            errors++;
            if (snprintf(errors_detail + strlen(errors_detail), sizeof(errors_detail) - strlen(errors_detail),
                    "第%d行: 未知交易类型 '%s'\n", line_num, tx_type_s) > 0) {}
            line_start = line_end ? line_end + 1 : line_start + 1;
            continue;
        }
```

> Fixes a pre-existing silent-failure path: a bad type previously made `csilk_db_query_param_json` return NULL while `imported++` still fired.

- [ ] **Step 2: Persist directions in the import INSERT (and fix asset_id binding bug)**

Replace the `ins_params` array + INSERT (lines 306-319) with (the existing `asset_id <= 0` guard at lines 263-269 stays untouched):

```c
        char asset_param[32];
        snprintf(asset_param, sizeof(asset_param), "%lld", (long long)asset_id);

        const char* ins_params[] = {
            uid_str, asset_param, linked_param, category_param,
            src_type, tx_type_s, ttype->stat_dir, ttype->linked_dir,
            amount_s, price_param, qty_param, currency,
            date_s, note_s ? note_s : "",
            NULL
        };

        csilk_json_t* res = csilk_db_query_param_json(pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, currency, "
            "transaction_date, note) "
            "VALUES (?, ?, NULLIF(?, '0'), NULLIF(?, '0'), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
            ins_params);
        if (res) csilk_json_free(res);
```

> **Pre-existing bug being fixed here:** the original code binds the raw asset **name** (`asset_name`) into the INTEGER `asset_id` column (line 307) — the resolved `asset_id` from lines 256-261 was never used, so CSV imports stored garbage/missing asset_id. This mirrors the correct `asset_param` pattern already used by `daily_expenses_import_csv` (lines 520-522).

- [ ] **Step 3: Include the registry header in `import_export.c`**

Add `#include "common/tx_types.h"` to the include block at the top of `backend/src/import_export.c` (lines 1-8). The registry already lives in the shared module created in Task 4 — no extraction needed.

- [ ] **Step 4: Rebuild + run suite**

Run: `cmake --build backend/build --parallel && cd backend && ./tests/test_link.sh`
Expected: build exit 0; PASS=55 FAIL=0.

- [ ] **Step 5: Commit**

```bash
git add backend/src/import_export.c
git commit -m "feat(backend): validate and persist direction on csv import"
```

---

### Task 10: Frontend — single TRANSACTION_TYPES source

**Files:**
- Modify: `frontend/src/types/index.ts:30-51`
- Modify: `frontend/src/views/Transactions.vue:394-472`

- [ ] **Step 1: Add Direction type + Transaction fields to `types/index.ts`**

After line 28 add `export type Direction = 'in' | 'out' | 'neutral'`. Inside `Transaction` (after `source_type`, line 41) add:

```ts
  direction?: Direction
  linked_direction?: Direction
```

- [ ] **Step 2: Replace the 4 scattered helpers with one constant**

In `Transactions.vue`, replace lines 394-404 (`transactionTypes`) and 446-472 (`isIncomeType`, `isTradingType`, `typeLabel`, `typeTag`) with:

```ts
interface TransactionTypeOption {
  value: Transaction['transaction_type']
  label: string
  direction: Direction
  isTrading: boolean
  tagType: 'success' | 'warning' | 'danger' | 'info' | 'primary'
}

// 单源类型注册表：与后端 TX_TYPES 保持同序同步
const TRANSACTION_TYPES: TransactionTypeOption[] = [
  { value: 'deposit', label: '存入', direction: 'in', isTrading: false, tagType: 'success' },
  { value: 'withdrawal', label: '取出', direction: 'out', isTrading: false, tagType: 'danger' },
  { value: 'buy', label: '买入', direction: 'out', isTrading: true, tagType: 'primary' },
  { value: 'sell', label: '卖出', direction: 'in', isTrading: true, tagType: 'warning' },
  { value: 'transfer_in', label: '转入', direction: 'in', isTrading: false, tagType: 'success' },
  { value: 'transfer_out', label: '转出', direction: 'out', isTrading: false, tagType: 'warning' },
  { value: 'fee', label: '手续费', direction: 'out', isTrading: false, tagType: 'info' },
  { value: 'income', label: '收益', direction: 'in', isTrading: false, tagType: 'success' },
  { value: 'loss', label: '亏损', direction: 'out', isTrading: false, tagType: 'danger' },
  { value: 'interest', label: '利息', direction: 'in', isTrading: false, tagType: 'success' },
]

const transactionTypes = TRANSACTION_TYPES.map(({ value, label }) => ({ value, label }))

const typeOption = (t: string) => TRANSACTION_TYPES.find(x => x.value === t)

function isIncomeType(t: string) {
  return typeOption(t)?.direction === 'in'
}

function isTradingType(t: string) {
  return typeOption(t)?.isTrading ?? false
}

function typeLabel(t: string) {
  return typeOption(t)?.label || t
}

function typeTag(t: string): 'success' | 'warning' | 'danger' | 'info' | 'primary' {
  return typeOption(t)?.tagType || 'info'
}
```

> Behavior identical for all 9 legacy types: the old `isIncomeType` list (`deposit, income, sell, transfer_in`) is exactly the set whose stat_dir is `in`; `isTradingType` = is_trading; `typeTag` map equals tagType. Existing template usages (`typeTag(row.transaction_type)`, `typeLabel(...)`, `isIncomeType(row.transaction_type)`, `isTradingType(form.transaction_type)`, `transactionTypes` in the form dropdown) are untouched and now read the single constant.

- [ ] **Step 3: Build frontend**

Run: `npm --prefix frontend run build`
Expected: `vue-tsc -b && vite build` exit 0, 0 errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/types/index.ts frontend/src/views/Transactions.vue
git commit -m "feat(frontend): single transaction type registry driving display and form"
```

---

### Task 11: New integration tests + full verification

**Files:**
- Modify: `backend/tests/test_link.sh` (append after line 267, before the final `echo "结果:..."`)

- [ ] **Step 1: Write the new test sections**

Append:

```bash
echo "== 24. 方向数据化：新类型 interest 驱动统计、余额、列表 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"interest\",\"amount\":200,\"currency\":\"CNY\",\"transaction_date\":\"2026-09-01\"}" >/dev/null
# 钱包余额在测试 14 后为 -9600.0，interest(+200, balance_dir=in) → -9400.0
BAL_AFTER=$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")
check "interest 余额联动 +200" "-9400.0" "$BAL_AFTER"
DIR_VAL=$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE transaction_type='interest' ORDER BY id DESC LIMIT 1")
check "interest direction 已持久化" "in" "$DIR_VAL"
MONTH9=$(curl -s -H "$AUTH" "$BASE/transactions/monthly?month=2026-09")
check "monthly 2026-09 inflows=200" "200" "$(echo "$MONTH9" | jq -r '.data.inflows | floor')"
check "monthly 2026-09 outflows=0" "0" "$(echo "$MONTH9" | jq -r '.data.outflows | floor')"
check "monthly 2026-09 count=1" "1" "$(echo "$MONTH9" | jq -r '.data.count | floor')"

echo "== 25. 未知交易类型 → 1002 且原子回滚 =="
TX_BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions")
CODE=$(curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"mystery\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-09-02\"}" | jq -r '.code | floor')
TX_AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM transactions")
check "未知类型 code=1002" "1002" "$CODE"
check "未知类型不落库" "$TX_BEFORE" "$TX_AFTER"
check "未知类型余额不变" "$BAL_AFTER" "$(sqlite3 "$DB" "SELECT printf('%.1f', current_value) FROM assets WHERE id=$WALLET_ID")"

echo "== 26. 导入 CSV 含新类型 interest =="
cat > /tmp/mf_import_tx.csv << 'CSVEOF'
date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,currency,linked_asset_name,note
2026-09-03,钱包,现金,interest,income,50,0,0,CNY,,导入利息
CSVEOF
IMP_RES=$(curl -s -H "$AUTH" -H 'Content-Type: text/csv; charset=utf-8' --data-binary @/tmp/mf_import_tx.csv "$BASE/import/transactions")
rm -f /tmp/mf_import_tx.csv
check "导入 interest imported=1" "1" "$(echo "$IMP_RES" | jq -r '.data.imported | floor')"
check "导入 interest errors=0" "0" "$(echo "$IMP_RES" | jq -r '.data.errors | floor')"
check "导入行 direction='in'" "in" "$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE note='导入利息' LIMIT 1")"

echo "== 27. 存量数据 direction 回填 =="
check "存量 deposit 行回填 direction='in'" "in" "$(sqlite3 "$DB" "SELECT direction FROM transactions WHERE transaction_type='deposit' ORDER BY id DESC LIMIT 1")"
check "存量 fee 行回填 linked_direction='out'" "out" "$(sqlite3 "$DB" "SELECT linked_direction FROM transactions WHERE transaction_type='fee' ORDER BY id DESC LIMIT 1")"
```

> Note: tests 24-27 create records with dates in 2026-09 so the 2026-08 monthly assertions in test 18 stay valid. Wallet balance is deterministic: `-9600.0 + 200 = -9400.0`.

- [ ] **Step 2: Run the full verification gate (AGENTS.md)**

Run: `cmake --build backend/build --parallel && npm --prefix frontend run build && cd backend && ./tests/test_link.sh`
Expected: build exit 0; frontend build 0 errors; `结果: PASS=68 FAIL=0` (55 existing + 13 new checks).

- [ ] **Step 3: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test: cover direction persistence, interest type, unknown type 1002, import and backfill"
```

---

### Task 12: Manual end-to-end smoke test (browser)

**Files:** none (verification only)

- [ ] **Step 1: Run backend + frontend**

Run: `cd backend && ./build/minefolio &` then `npm --prefix frontend run dev` (use port 5174 if 5173 is occupied: `npm --prefix frontend run dev -- --port 5174`).

- [ ] **Step 2: Drive the real UI with playwright-core**

Reuse the headless-chrome harness (playwright-core@1.45 + `/opt/google/chrome/chrome`, inject JWT into localStorage). Flow:
1. Login → Dashboard renders.
2. 交易 page → create a transaction with type 利息 (interest) → confirm amount shows `+` and balance of wallet increases.
3. 报表 page → confirm it renders (performance endpoint no longer mis-classifies).
4. Verify the daily-expense blank-page fix still holds: 收支 → 仪表盘 → 交易 → all render.

- [ ] **Step 3: Report results**

Record page renders + console errors (expect zero `[Vue warn]` about Transition).

---

## Verification Gate (run before declaring completion)

```bash
cmake --build backend/build --parallel          # exit 0
npm --prefix frontend run build                 # 0 errors
cd backend && ./tests/test_link.sh              # PASS=68 FAIL=0
```

## Records & Notes

- Commits are intentionally one-per-task (11 commits total) so `git bisect` and review stay clean. Do not squash them.
- Do NOT push — pushing requires explicit user request.
- The `interest` type is the living proof that the registry works: adding it required touching ONLY the registry, the frontend constant, and tests — no SQL list edits.
- Keeping `transaction_type NOT IN ('transfer_in','transfer_out')` in the performance query is a deliberate semantic exclusion (transfers are neither gain nor loss), not a type-list hack.