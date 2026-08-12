# 交易关联资金账户双向联动 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow users to link a funding asset account (e.g. Bank Card) when creating/editing trading transactions, automatically deducting funds on buy/deposit and returning funds on sell/withdrawal.

**Architecture:** Extend SQLite `transactions` table with `linked_asset_id`, update C backend handlers (`transactions.c`, `db.c`) to apply bi-directional deltas via `balance_apply_delta`, and update Vue 3 `Transactions.vue` to allow selecting funding assets and displaying them in the data table.

**Tech Stack:** C23, SQLite, Vue 3, TypeScript, Element Plus.

---

### Task 1: Database Migration (`migration.sql` & `db.c`)

**Files:**
- Modify: `backend/sql/migration.sql:40-60`
- Modify: `backend/src/common/db.c:85-115`

- [ ] **Step 1: Update `migration.sql` schema definition**

Update `transactions` table definition in `backend/sql/migration.sql` to include `linked_asset_id`:

```sql
CREATE TABLE IF NOT EXISTS transactions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset_id         INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    linked_asset_id  INTEGER REFERENCES assets(id) ON DELETE SET NULL,
    category_id      INTEGER REFERENCES categories(id) ON DELETE RESTRICT,
...
```

- [ ] **Step 2: Add migration check in `db.c` for existing databases**

Add `linked_asset_id` column check in `db_run_migrations()` in `backend/src/common/db.c`:

```c
    // ---- transactions 表 linked_asset_id 列迁移 ----
    int has_linked_asset = 0;
    csilk_json_t* tx_cols = csilk_db_query_json(pool, "PRAGMA table_info(transactions)");
    if (tx_cols) {
        size_t n = csilk_json_array_size(tx_cols);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* col = csilk_json_array_get(tx_cols, i);
            const char* cname = csilk_json_get_string(col, "name");
            if (cname && strcmp(cname, "linked_asset_id") == 0) { has_linked_asset = 1; break; }
        }
        csilk_json_free(tx_cols);
    }
    if (!has_linked_asset) {
        csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL");
    }
```

- [ ] **Step 3: Compile backend and run migrations**

Run: `cmake --build backend/build && ./backend/build/minefolio`
Expected: Compiles cleanly and executes migration without errors.

- [ ] **Step 4: Commit**

```bash
git add backend/sql/migration.sql backend/src/common/db.c
git commit -m "feat(db): add linked_asset_id column to transactions table and migration check"
```

---

### Task 2: Backend Handler Updates (`backend/src/transactions.c`)

**Files:**
- Modify: `backend/src/transactions.c:30-330`

- [ ] **Step 1: Update `transactions_list` SQL query**

Update `transactions_list()` in `backend/src/transactions.c` to `LEFT JOIN assets la ON t.linked_asset_id=la.id` and select `t.linked_asset_id`, `la.name as linked_asset_name`. Include `linked_asset_id` and `linked_asset_name` in the returned JSON objects.

- [ ] **Step 2: Update `transactions_create` for bi-directional deltas**

In `transactions_create()`:
1. Parse `int64_t linked_id = db_get_int(body, "linked_asset_id");`.
2. Check if `linked_id > 0`, verify it belongs to user and `linked_id != asset_id`.
3. Compute `linked_delta`:
   - `buy` / `deposit` / `fee` / `loss`: `linked_delta = -amount`
   - `sell` / `withdrawal` / `income`: `linked_delta = +amount`
4. Insert `linked_asset_id` into `transactions` table.
5. In transaction block, call `balance_apply_delta(pool, asset_id, user_id, tdelta, "transaction", tx_id, note)`.
6. If `linked_id > 0`, call `balance_apply_delta(pool, linked_id, user_id, linked_delta, "transaction_linked", tx_id, note)`.

- [ ] **Step 3: Update `transactions_update` and `transactions_delete`**

Update `transactions_update()` and `transactions_delete()` to read `old_linked_asset_id` and calculate old/new delta differences for `linked_asset_id`, applying changes via `balance_apply_delta`.

- [ ] **Step 4: Compile and test backend**

Run: `cmake --build backend/build`
Expected: 0 warnings, 0 errors.

- [ ] **Step 5: Commit**

```bash
git add backend/src/transactions.c
git commit -m "feat(backend): add linked_asset_id support and bi-directional balance deltas in transactions.c"
```

---

### Task 3: Backend Integration Test (`backend/tests/test_link.sh`)

**Files:**
- Modify: `backend/tests/test_link.sh`

- [ ] **Step 1: Add test case 14 for linked asset buy transaction**

In `test_link.sh`, add a test verifying:
1. Creating a `buy` transaction on asset A (Fund) linked to asset B (Cash) with amount 500.
2. Asset A balance increases by +500.
3. Asset B balance decreases by -500.
4. `GET /api/transactions` returns `linked_asset_id` and `linked_asset_name`.

- [ ] **Step 2: Run test suite**

Run: `cd backend && ./tests/test_link.sh`
Expected: PASS=17 FAIL=0 (or 16 PASS mandatory).

- [ ] **Step 3: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test(backend): add integration test for linked asset transaction balance deduction"
```

---

### Task 4: Frontend Types & Transactions Page Component (`Transactions.vue`)

**Files:**
- Modify: `frontend/src/types/index.ts`
- Modify: `frontend/src/views/Transactions.vue`

- [ ] **Step 1: Update TypeScript interface `Transaction`**

In `frontend/src/types/index.ts`, update `Transaction`:
```ts
export interface Transaction {
  id: number
  user_id: number
  asset_id: number
  linked_asset_id?: number | null
  asset_name?: string
  linked_asset_name?: string
  // ...
}
```

- [ ] **Step 2: Update `Transactions.vue` form & table**

In `frontend/src/views/Transactions.vue`:
1. Add `linked_asset_id: null as number | null` to `form` reactive object.
2. Add `<el-form-item label="资金账户">` in create/edit modal dialog with `<el-select v-model="form.linked_asset_id" placeholder="支付/扣款/回流账户（可选）" clearable>` listing `allAssets`.
3. Add `<el-table-column label="扣款/回流账户" min-width="120">` in `.premium-table` rendering `row.linked_asset_name` (or `-`).

- [ ] **Step 3: Run frontend build**

Run: `npm --prefix frontend run build`
Expected: vue-tsc & vite build succeed with 0 errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/types/index.ts frontend/src/views/Transactions.vue
git commit -m "feat(frontend): add linked_asset_id selector and column to Transactions view"
```
