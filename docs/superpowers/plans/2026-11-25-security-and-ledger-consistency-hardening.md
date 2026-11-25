# Security & Ledger Consistency Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the 5 remaining P0/P1 security and ledger consistency vulnerabilities from `docs/superpowers/specs/2026-11-25-security-and-ledger-consistency-hardening-design.md` across V020 snapshot invalidation, CSV export/import RBAC & ledger isolation, portfolio/DCA/cashflow shared-ledger RBAC alignment, and legacy IM gateway atomic `0600` credential persistence.

**Architecture:** Push critical invariants into single authoritative boundaries instead of scattered call-site patches: centralize `ledger_snapshot_invalidate_from()` inside `core/ledger/ledger_engine.c` with owner-compatible shared-ledger deletion, reuse `account_permission_repo_impl.h` SQL predicates (`MF_LEDGER_SHARED_ACCESS_*` and `MF_RBAC_ASSET_*`) across export/import/portfolio/DCA/cashflow, and share `writeJsonAtomic()` from `gateway/ilink/state.js` for `gateway/index.js`. Zero new DB migrations (`V036`) are introduced; legacy `NULL`/`0` `ledger_id` rows fall back cleanly to their parent asset's `ledger_id`.

**Tech Stack:** C23 (`csilk`, SQLite3, PostgreSQL `libpq`, CMake Unix Makefiles, CTest, ASan/UBSan), Bash integration suites, Node.js >= 18 (`node:test`).

---

## File Structure Map

| File | Responsibility in This Plan |
|---|---|
| `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c` | Allow shared-ledger editors to invalidate the asset owner's `ledger_snapshots` rows |
| `backend/src/core/ledger/ledger_engine.h`, `ledger_engine.c` | Auto-invalidate snapshots inside `ledger_apply_tx`, `ledger_reverse_tx`, and new `*_dated` expense/transfer operators |
| `backend/src/domain/daily_expense/entity.h`, `infrastructure/repositories/daily_expense_repo_impl.c` | Add `expense_date` to `mf_daily_expense_snapshot_t` so reversals invalidate from the historical expense date |
| `backend/src/application/{daily_expense,transfer,cashflow,transaction}/usecases.c` | Call `*_dated` ledger operators; add snapshot invalidation + target `ledger_id` handling to `cashflow_usecase_confirm_income`; drop redundant outer invalidations in `transaction/usecases.c` |
| `backend/src/services/export_service.h`, `export_service.c` | Enforce `MF_LEDGER_SHARED_ACCESS_SQL` + `MF_RBAC_ASSET_UNMASKED_SQL` via testable query helpers |
| `backend/src/services/import_service.h`, `import_service.c` | Enforce 10 MB upload cap, ledger-scoped + `MF_RBAC_ASSET_VISIBLE_SQL` anti-enumeration asset/category lookup, strict `linked_asset_name` lookup, per-row `MF_ACCOUNT_ACTION_WRITE_TX`, and dated expense snapshot invalidation |
| `backend/src/infrastructure/repositories/account_permission_repo_impl.h` | Add `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL` and `MF_LEDGER_SHARED_WRITE_FALLBACK_SQL` macros |
| `backend/src/infrastructure/repositories/portfolio_repo_impl.c` | Switch all 9 `ledger_id > 0` query branches to shared-ledger predicates |
| `backend/src/domain/dca/{entity,repository}.h`, `application/dca/{commands,usecases}.h/.c`, `infrastructure/repositories/dca_repo_impl.c`, `interfaces/http/controllers/dca_controller.c` | Thread `ledger_id` end-to-end, enforce `viewer`/`editor`, same-ledger asset pairing, `MF_ACCOUNT_ACTION_WRITE_TX`, and RBAC visibility/masking |
| `backend/src/domain/cashflow/{entity,repository}.h`, `application/cashflow/{commands,usecases}.h/.c`, `infrastructure/repositories/cashflow_repo_impl.{h,c}`, `interfaces/http/controllers/cashflow_controller.c` | Thread `ledger_id` end-to-end, enforce `viewer`/`editor`, same-ledger pairing, `MF_ACCOUNT_ACTION_WRITE_TX` on delete, and RBAC visibility/unmasked calendar totals |
| `gateway/ilink/state.js`, `gateway/index.js` | Add `{ tightenDir = true }` to `writeJsonAtomic()`; use it in `saveCache()` and tighten permissions on `loadCache()` |
| `backend/tests/unit/test_ledger_rebuild_checkpoint.c`, `test_account_rbac.c`, `test_domain_repositories.c`, `gateway/adapters/test/cache.test.js` | TDD regression suites for every module |

---

### Task 1: Centralize V020 Ledger Snapshot Invalidation in Ledger Engine & Callers

**Files:**
- Modify: `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c:164-190`
- Modify: `backend/src/core/ledger/ledger_engine.h:127-170`
- Modify: `backend/src/core/ledger/ledger_engine.c:1-25,360-425,590-618,890-983`
- Modify: `backend/src/domain/daily_expense/entity.h:30-38`
- Modify: `backend/src/infrastructure/repositories/daily_expense_repo_impl.c:440-471`
- Modify: `backend/src/application/daily_expense/usecases.c:42-75,447-460,556-604,666-680`
- Modify: `backend/src/application/transfer/usecases.c:153-166`
- Modify: `backend/src/application/cashflow/usecases.c:1-16,440-490`
- Modify: `backend/src/application/transaction/usecases.c:265-275,516-537,713-724`
- Test: `backend/tests/unit/test_ledger_rebuild_checkpoint.c:315-328`

- [ ] **Step 1: Write failing tests in `backend/tests/unit/test_ledger_rebuild_checkpoint.c`**

Update the top includes and insert Test 6 right before the final `printf("=== All Ledger Rebuild Checkpoint Tests Passed! ===\n");` at `backend/tests/unit/test_ledger_rebuild_checkpoint.c:325`:

```c
// Add at top of backend/tests/unit/test_ledger_rebuild_checkpoint.c (after line 2):
#include "application/cashflow/usecases.h"
```

```c
    /* -------------------------------------------------------------
     * Test 6: Engine-centralized snapshot invalidation across all mutation paths
     *         including shared-ledger editor invalidating owner's snapshot
     * ------------------------------------------------------------- */
    printf("Test 6: Centralized snapshot invalidation (apply/reverse/transfer/expense/cashflow/shared)...\n");

    // Seed shared ledger & member user 2 (editor) for asset 201 and 101
    csilk_db_exec(pool, "INSERT INTO users (id, username, password) VALUES (2, 'editor_bob', 'hash456')");
    csilk_db_exec(pool, "INSERT INTO ledgers (id, owner_id, name) VALUES (1, 1, 'Shared Ledger')");
    csilk_db_exec(pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (1, 1, 'owner'), (1, 2, 'editor')");
    csilk_db_exec(pool, "UPDATE assets SET ledger_id = 1 WHERE id IN (101, 201)");

    // Helper: save a snapshot at 2026-09-10 for asset 201 (owner=1)
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);

    // 6a. Backdated ledger_apply_tx WITHOUT manual ledger_snapshot_invalidate_from must auto-invalidate
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    ledger_tx_t tx6a = {.id = 0,
                        .user_id = user_id,
                        .asset_id = 201,
                        .linked_asset_id = 101,
                        .category_id = 20,
                        .type = LEDGER_TX_BUY,
                        .type_str = "buy",
                        .amount = b1_amt,
                        .price = b1_price,
                        .quantity = b1_qty,
                        .fee = b1_fee,
                        .tx_date = "2026-09-05 10:00:00",
                        .note = "Auto invalidate apply_tx",
                        .ledger_id = 1};
    assert(ledger_apply_tx(pool, &tx6a) == 0);
    csilk_db_exec(pool, "COMMIT");
    snap = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0);
    assert(snap == NULL);

    // 6b. Shared-ledger editor (user_id=2) reversing tx6a must invalidate owner's (user_id=1) snapshot
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx6a.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx6a.id, "[]") == 0);
    csilk_db_exec(pool, "BEGIN TRANSACTION");
    assert(ledger_reverse_tx(pool, 2, tx6a.id) == 0);
    csilk_db_exec(pool, "COMMIT");
    snap = NULL;
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0 && snap == NULL);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    // 6c. Backdated transfer (ledger_apply_transfer_dated) auto-invalidates both assets
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 201, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    money_t xfer_amt;
    money_from_double(100.0, CURRENCY_CNY, &xfer_amt);
    assert(ledger_apply_transfer_dated(
               pool, user_id, 101, 201, xfer_amt, 9001, "Backdated xfer", "2026-09-08") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);
    assert(ledger_snapshot_get_latest(pool, user_id, 201, NULL, &snap) == 0 && snap == NULL);

    // 6d. Backdated daily expense apply & reverse auto-invalidate target snapshot
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_apply_expense_dated(
               pool, user_id, 101, xfer_amt, 0, 9002, "Backdated exp", "2026-09-07") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    assert(ledger_reverse_expense_dated(
               pool, user_id, 101, xfer_amt, 0, 9002, "Reverse exp", "2026-09-07") == 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap == NULL);

    // 6e. Cashflow confirm income auto-invalidates target asset snapshot while preserving earlier snapshot
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-01", &pos5, zero_bal, "2026-09-01 10:00:00", tx1.id, "[]") == 0);
    assert(ledger_snapshot_save(
               pool, user_id, 101, "2026-09-10", &pos5, zero_bal, "2026-09-10 10:00:00", tx4.id, "[]") == 0);
    confirm_cashflow_cmd_t cf_cmd = {
        .user_id = user_id,
        .source_asset_id = 201,
        .target_asset_id = 101,
        .amount = 50.0,
        .date = "2026-09-06",
        .name = "Dividend",
        .note = "Q3",
    };
    cashflow_usecase_result_t cf_res = {0};
    int64_t cf_tx_id = 0;
    assert(cashflow_usecase_confirm_income(pool, &cf_cmd, &cf_tx_id, &cf_res) == 0 && cf_tx_id > 0);
    assert(ledger_snapshot_get_latest(pool, user_id, 101, NULL, &snap) == 0 && snap != NULL);
    assert(strcmp(csilk_json_get_string(snap, "snapshot_date"), "2026-09-01") == 0);
    csilk_json_free(snap);
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run: `cmake --build backend/build --target test_ledger_rebuild_checkpoint`
Expected: FAIL with implicit declaration / undefined reference to `ledger_apply_transfer_dated`, `ledger_apply_expense_dated`, `ledger_reverse_expense_dated`.

- [ ] **Step 3: Implement snapshot owner cleanup + `ledger_engine` centralized invalidation**

1. In `backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c:164-190`, replace `ledger_snapshot_invalidate_from`:

```c
int
ledger_snapshot_invalidate_from(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int64_t          asset_id,
                                const char*      from_date)
{
    if (!pool || user_id <= 0 || asset_id <= 0 || !from_date || strlen(from_date) < 10) {
        return -1;
    }

    char uid_str[32], ast_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(ast_str, sizeof(ast_str), "%lld", (long long)asset_id);

    char from_day[16] = {0};
    snprintf(from_day, sizeof(from_day), "%.10s", from_date);

    const char* sql =
        "DELETE FROM ledger_snapshots "
        "WHERE asset_id=? AND snapshot_date >= ? "
        "AND (user_id=? OR user_id=(SELECT user_id FROM assets WHERE id=?))";
    const char*   params[] = {ast_str, from_day, uid_str, ast_str, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res) {
        return -1;
    }
    csilk_json_free(res);
    return 0;
}
```

2. In `backend/src/core/ledger/ledger_engine.h`, add declarations right after `ledger_reverse_transfer` (lines 163-170):

```c
/**
 * @brief 按指定业务日期应用日常收支事件并自动失效该日起的检查点快照
 */
int ledger_apply_expense_dated(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               money_t          amount,
                               int              is_income,
                               int64_t          expense_id,
                               const char*      note,
                               const char*      expense_date);

/**
 * @brief 按指定业务日期回滚日常收支事件并自动失效该日起的检查点快照
 */
int ledger_reverse_expense_dated(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          asset_id,
                                 money_t          amount,
                                 int              is_income,
                                 int64_t          expense_id,
                                 const char*      note,
                                 const char*      expense_date);

/**
 * @brief 按指定业务日期应用划转事件并自动失效双方资产该日起的检查点快照
 */
int ledger_apply_transfer_dated(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                int64_t          from_asset_id,
                                int64_t          to_asset_id,
                                money_t          amount,
                                int64_t          transfer_id,
                                const char*      note,
                                const char*      transfer_date);
```

3. In `backend/src/core/ledger/ledger_engine.c`:
   - Add `#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"` near the top includes.
   - Add helper `invalidate_asset_snapshot_safe`:

```c
static int
invalidate_asset_snapshot_safe(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               const char*      event_date)
{
    if (asset_id <= 0) {
        return 0;
    }
    char day_buf[32];
    if (event_date && strlen(event_date) >= 10) {
        snprintf(day_buf, sizeof(day_buf), "%.10s", event_date);
    } else {
        format_now_timestamp(day_buf, sizeof(day_buf));
        day_buf[10] = '\0';
    }
    return ledger_snapshot_invalidate_from(pool, user_id, asset_id, day_buf);
}
```

   - Update `apply_expense_delta` and `apply_transfer_deltas` to take `const char* event_date` and call `invalidate_asset_snapshot_safe` before `db_tx_scope_commit` (rolling back on `< 0`):

```c
static int
apply_expense_delta(csilk_db_pool_t* pool,
                    int64_t          user_id,
                    int64_t          asset_id,
                    money_t          delta,
                    const char*      source_type,
                    int64_t          expense_id,
                    const char*      note,
                    const char*      event_date,
                    const char*      scope_name)
{
    if (!pool || user_id <= 0 || asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, scope_name, &scope) != 0) {
        return -1;
    }
    if (balance_apply_delta_m(pool, asset_id, user_id, delta, source_type, expense_id, note) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (invalidate_asset_snapshot_safe(pool, user_id, asset_id, event_date) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    db_tx_scope_commit(pool, &scope);
    return 0;
}

static int
apply_transfer_deltas(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          from_asset_id,
                      int64_t          to_asset_id,
                      money_t          from_delta,
                      money_t          to_delta,
                      const char*      from_source,
                      const char*      to_source,
                      int64_t          transfer_id,
                      const char*      note,
                      const char*      event_date,
                      const char*      scope_name)
{
    if (!pool || user_id <= 0 || from_asset_id <= 0 || to_asset_id <= 0) {
        return -1;
    }
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, scope_name, &scope) != 0) {
        return -1;
    }
    if (balance_apply_delta_m(
            pool, from_asset_id, user_id, from_delta, from_source, transfer_id, note) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (balance_apply_delta_m(pool, to_asset_id, user_id, to_delta, to_source, transfer_id, note) !=
        0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    if (invalidate_asset_snapshot_safe(pool, user_id, from_asset_id, event_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, user_id, to_asset_id, event_date) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    db_tx_scope_commit(pool, &scope);
    return 0;
}
```

   - In `ledger_apply_tx` (before `db_tx_scope_commit(pool, &scope);` at line 609):

```c
    if (invalidate_asset_snapshot_safe(pool, tx->user_id, tx->asset_id, effective_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, tx->user_id, tx->linked_asset_id, effective_date) !=
            0) {
        goto fail_rollback;
    }
```

   - In `ledger_reverse_tx` (before `csilk_json_free(old_arr);` at line 897):

```c
    if (invalidate_asset_snapshot_safe(pool, user_id, asset_id, rev_date) != 0 ||
        invalidate_asset_snapshot_safe(pool, user_id, linked_asset_id, rev_date) != 0) {
        goto fail_rollback;
    }
```

   - Implement `ledger_apply_expense_dated`, `ledger_reverse_expense_dated`, `ledger_apply_transfer_dated` and update the wrappers at lines 907-983:

```c
int
ledger_apply_expense_dated(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          asset_id,
                           money_t          amount,
                           int              is_income,
                           int64_t          expense_id,
                           const char*      note,
                           const char*      expense_date)
{
    money_t delta = is_income ? amount : money_neg(amount);
    return apply_expense_delta(pool,
                               user_id,
                               asset_id,
                               delta,
                               "daily_expense",
                               expense_id,
                               note,
                               expense_date,
                               "mf_ledger_apply_exp");
}

int
ledger_apply_expense(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          asset_id,
                     money_t          amount,
                     int              is_income,
                     int64_t          expense_id,
                     const char*      note)
{
    return ledger_apply_expense_dated(
        pool, user_id, asset_id, amount, is_income, expense_id, note, NULL);
}

int
ledger_reverse_expense_dated(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          asset_id,
                             money_t          amount,
                             int              is_income,
                             int64_t          expense_id,
                             const char*      note,
                             const char*      expense_date)
{
    money_t delta = is_income ? money_neg(amount) : amount;
    return apply_expense_delta(pool,
                               user_id,
                               asset_id,
                               delta,
                               "daily_expense_reversal",
                               expense_id,
                               note,
                               expense_date,
                               "mf_ledger_reverse_exp");
}

int
ledger_reverse_expense(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          asset_id,
                       money_t          amount,
                       int              is_income,
                       int64_t          expense_id,
                       const char*      note)
{
    return ledger_reverse_expense_dated(
        pool, user_id, asset_id, amount, is_income, expense_id, note, NULL);
}

int
ledger_apply_transfer_dated(csilk_db_pool_t* pool,
                            int64_t          user_id,
                            int64_t          from_asset_id,
                            int64_t          to_asset_id,
                            money_t          amount,
                            int64_t          transfer_id,
                            const char*      note,
                            const char*      transfer_date)
{
    return apply_transfer_deltas(pool,
                                 user_id,
                                 from_asset_id,
                                 to_asset_id,
                                 money_neg(amount),
                                 amount,
                                 "transfer_out",
                                 "transfer_in",
                                 transfer_id,
                                 note,
                                 transfer_date,
                                 "mf_ledger_apply_transfer");
}

int
ledger_apply_transfer(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          from_asset_id,
                      int64_t          to_asset_id,
                      money_t          amount,
                      int64_t          transfer_id,
                      const char*      note)
{
    return ledger_apply_transfer_dated(
        pool, user_id, from_asset_id, to_asset_id, amount, transfer_id, note, NULL);
}

int
ledger_reverse_transfer(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          from_asset_id,
                        int64_t          to_asset_id,
                        money_t          amount,
                        int64_t          transfer_id,
                        const char*      note)
{
    return apply_transfer_deltas(pool,
                                 user_id,
                                 from_asset_id,
                                 to_asset_id,
                                 amount,
                                 money_neg(amount),
                                 "transfer_out_reversal",
                                 "transfer_in_reversal",
                                 transfer_id,
                                 note,
                                 NULL,
                                 "mf_ledger_reverse_transfer");
}
```

- [ ] **Step 4: Wire `expense_date`, `transfer_date`, `cashflow_confirm` invalidation, and remove duplicate `transaction/usecases.c` invalidations**

1. In `backend/src/domain/daily_expense/entity.h:32-38`:

```c
typedef struct {
    double  amount;
    char    expense_type[16];
    char    currency[8];
    int64_t asset_id;
    char    expense_date[16];
    char    note[256];
} mf_daily_expense_snapshot_t;
```

2. In `backend/src/infrastructure/repositories/daily_expense_repo_impl.c:447-470`:

```c
    csilk_json_t* row = csilk_db_query_param_json(
        (csilk_db_pool_t*)db_pool,
        "SELECT amount,expense_type,asset_id,currency,CAST(expense_date AS TEXT) AS expense_date,note FROM "
        "daily_expenses WHERE id=? AND (user_id=? OR (ledger_id=? AND EXISTS "
        "(SELECT 1 FROM ledger_members lm WHERE lm.ledger_id=daily_expenses.ledger_id AND "
        "lm.user_id=?)))",
        (const char*[]){idstr, uid, lid, uid, NULL});
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        return -1;
    }
    const csilk_json_t* r = csilk_json_array_get(row, 0);
    out->amount = db_get_num(r, "amount");
    const char* et = csilk_json_get_string(r, "expense_type");
    strncpy(out->expense_type, et ? et : "", sizeof(out->expense_type) - 1);
    const char* cur = csilk_json_get_string(r, "currency");
    strncpy(out->currency, cur && cur[0] ? cur : "CNY", sizeof(out->currency) - 1);
    out->asset_id = (int64_t)db_get_int(r, "asset_id");
    const char* ed = csilk_json_get_string(r, "expense_date");
    if (ed) {
        snprintf(out->expense_date, sizeof(out->expense_date), "%.10s", ed);
    }
    const char* nt = csilk_json_get_string(r, "note");
    strncpy(out->note, nt ? nt : "", sizeof(out->note) - 1);
    csilk_json_free(row);
    return 0;
```

3. In `backend/src/application/daily_expense/usecases.c`:
   - Update `apply_balance` and `reverse_balance` to accept `const char* expense_date` and call `ledger_apply_expense_dated` / `ledger_reverse_expense_dated`; pass `cmd->expense_date` at lines 455 & 598, and `old.expense_date` at lines 564 & 674.

4. In `backend/src/application/transfer/usecases.c:158`:

```c
    if (ledger_apply_transfer_dated(pool,
                                    cmd->user_id,
                                    cmd->from_asset_id,
                                    cmd->to_asset_id,
                                    amt_m,
                                    transfer_id,
                                    note,
                                    cmd->transfer_date) != 0) {
```

5. In `backend/src/application/cashflow/usecases.c`:
   - Add `#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"` at top.
   - Right after `balance_apply_delta(...)` at line 482 and before `db_tx_scope_commit` at line 484:

```c
    if (ledger_snapshot_invalidate_from(
            db_pool, cmd->user_id, cmd->target_asset_id, full_date) != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "清理账本快照失败");
        return -1;
    }
```

6. In `backend/src/application/transaction/usecases.c`:
   - Remove the redundant manual `ledger_snapshot_invalidate_from(...)` blocks at lines 265-273 (`tx_usecase_create`), lines 516-536 (`tx_usecase_update`), and lines 713-723 (`tx_usecase_delete`), because `ledger_apply_tx` and `ledger_reverse_tx` now execute them inside the savepoint scope on all branches. Remove `#include "infrastructure/repositories/ledger_snapshot_repo_impl.h"` from `application/transaction/usecases.c` if unused.

- [ ] **Step 5: Build and run snapshot & ledger unit tests**

Run:
```bash
cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R "test_ledger_rebuild_checkpoint|test_ledger_engine|test_cost_basis_fifo|test_transaction_lots|test_tx_scope_nesting" --output-on-failure
```
Expected: All 5 suites PASS (`100% tests passed, 0 tests failed`).

- [ ] **Step 6: Commit Task 1**

```bash
git add backend/src/infrastructure/repositories/ledger_snapshot_repo_impl.c \
        backend/src/core/ledger/ledger_engine.h \
        backend/src/core/ledger/ledger_engine.c \
        backend/src/domain/daily_expense/entity.h \
        backend/src/infrastructure/repositories/daily_expense_repo_impl.c \
        backend/src/application/daily_expense/usecases.c \
        backend/src/application/transfer/usecases.c \
        backend/src/application/cashflow/usecases.c \
        backend/src/application/transaction/usecases.c \
        backend/tests/unit/test_ledger_rebuild_checkpoint.c
git commit -m "fix(ledger): 🐛 centralize snapshot invalidation in ledger engine"
```

---

### Task 2: Harden CSV Export with Shared Ledger Access & Account RBAC (`export_service`)

**Files:**
- Modify: `backend/src/services/export_service.h:1-20`
- Modify: `backend/src/services/export_service.c:1-90,203-235`
- Test: `backend/tests/unit/test_account_rbac.c:1-20,605-628`

- [ ] **Step 1: Write failing test in `backend/tests/unit/test_account_rbac.c`**

Include `"services/export_service.h"` at top of `test_account_rbac.c`, add `test_csv_export_rbac_and_shared_ledger(pool)` and call it from `main()` after `test_usecases_and_masking(pool)`:

```c
static void
test_csv_export_rbac_and_shared_ledger(csilk_db_pool_t* pool)
{
    printf("--- Testing CSV Export Shared Ledger & Account RBAC ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "exp_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "exp_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* lrow = csilk_db_query_param_json(
        pool,
        "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ExportShared', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    assert(lrow && csilk_json_array_size(lrow) == 1);
    int64_t ledger_id = db_get_int(csilk_json_array_get(lrow, 0), "id");
    csilk_json_free(lrow);

    char s_lid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)ledger_id);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'viewer');",
        (const char*[]){s_lid, s_oid, s_lid, s_mid, NULL});

    // Seed categories for owner
    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (?, ?, 'ExportCash', 'asset', 'cash');",
        (const char*[]){s_oid, s_lid, NULL});
    csilk_json_t* crow = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ExportCash';",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t cash_cat = db_get_int(csilk_json_array_get(crow, 0), "id");
    csilk_json_free(crow);

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, 'ExportFood', 'expense');",
        (const char*[]){s_oid, s_lid, NULL});
    crow = csilk_db_query_param_json(
        pool,
        "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ExportFood';",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t exp_cat = db_get_int(csilk_json_array_get(crow, 0), "id");
    csilk_json_free(crow);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpOpen", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpMask", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_none = {.user_id = owner_id, .ledger_id = ledger_id, .name = "ExpNone", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0);
    int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0);
    int64_t acct_mask = acres.id;
    assert(account_usecase_create(pool, &ac_none, &acres) == 0);
    int64_t acct_none = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_open = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_open, .name = "OpenCash", .current_value = 1000.0, .currency = "CNY"};
    create_asset_cmd_t as_mask = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_mask, .name = "MaskCash", .current_value = 2000.0, .currency = "CNY"};
    create_asset_cmd_t as_none = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cash_cat, .account_id = acct_none, .name = "NoneCash", .current_value = 3000.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_open, &ares) == 0);
    int64_t asset_open = ares.id;
    assert(asset_usecase_create(pool, &as_mask, &ares) == 0);
    int64_t asset_mask = ares.id;
    assert(asset_usecase_create(pool, &as_none, &ares) == 0);
    int64_t asset_none = ares.id;

    tx_usecase_result_t txres = {0};
    create_tx_cmd_t tx_open = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_open, .category_id = exp_cat, .type = "income", .amount = 111.0, .currency = "CNY", .date = "2026-11-21", .note = "open_tx"};
    create_tx_cmd_t tx_mask = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_mask, .category_id = exp_cat, .type = "income", .amount = 222.0, .currency = "CNY", .date = "2026-11-21", .note = "mask_tx"};
    create_tx_cmd_t tx_none = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_none, .category_id = exp_cat, .type = "income", .amount = 333.0, .currency = "CNY", .date = "2026-11-21", .note = "none_tx"};
    create_tx_cmd_t tx_cross = {.user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_open, .linked_asset_id = asset_mask, .category_id = exp_cat, .type = "income", .amount = 444.0, .currency = "CNY", .date = "2026-11-21", .note = "linked_mask_tx"};
    assert(tx_usecase_create(pool, &tx_open, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_mask, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_none, &txres) == 0);
    assert(tx_usecase_create(pool, &tx_cross, &txres) == 0);

    daily_expense_usecase_result_t deres = {0};
    create_daily_expense_cmd_t de_open = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_open, .expense_type = "expense", .amount = 15.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "open_exp"};
    create_daily_expense_cmd_t de_mask = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_mask, .expense_type = "expense", .amount = 25.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "mask_exp"};
    create_daily_expense_cmd_t de_none = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = exp_cat, .asset_id = asset_none, .expense_type = "expense", .amount = 35.0, .currency = "CNY", .expense_date = "2026-11-21", .note = "none_exp"};
    assert(daily_expense_usecase_create(pool, &de_open, NULL, &deres) == 0);
    assert(daily_expense_usecase_create(pool, &de_mask, NULL, &deres) == 0);
    assert(daily_expense_usecase_create(pool, &de_none, NULL, &deres) == 0);

    // Restrict member: mask on acct_mask, none on acct_none
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_mask, member_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_none, member_id, "none", 0, 0, &acres) == 0);

    // Member exports: sees ONLY open_tx and open_exp (owner rows shared, masked/none excluded)
    csilk_json_t* tx_rows = export_query_transactions_rows(pool, member_id, ledger_id);
    assert(tx_rows != NULL && csilk_json_array_size(tx_rows) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(tx_rows, 0), "note"), "open_tx") == 0);
    csilk_json_free(tx_rows);

    csilk_json_t* de_rows = export_query_daily_expenses_rows(pool, member_id, ledger_id);
    assert(de_rows != NULL && csilk_json_array_size(de_rows) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(de_rows, 0), "note"), "open_exp") == 0);
    csilk_json_free(de_rows);

    // Owner still exports all 4 tx and 3 expenses
    tx_rows = export_query_transactions_rows(pool, owner_id, ledger_id);
    assert(tx_rows != NULL && csilk_json_array_size(tx_rows) == 4);
    csilk_json_free(tx_rows);

    de_rows = export_query_daily_expenses_rows(pool, owner_id, ledger_id);
    assert(de_rows != NULL && csilk_json_array_size(de_rows) == 3);
    csilk_json_free(de_rows);
    printf("  [OK] CSV export shares owner rows and excludes masked/none accounts\n");
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run: `cmake --build backend/build --target test_account_rbac`
Expected: FAIL with implicit declaration of `export_query_transactions_rows` and `export_query_daily_expenses_rows`.

- [ ] **Step 3: Implement RBAC + shared-ledger query helpers in `export_service.h` & `export_service.c`**

1. In `backend/src/services/export_service.h`, include `"common/db.h"` and `<stdint.h>` and declare:

```c
csilk_json_t*
export_query_transactions_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);

csilk_json_t*
export_query_daily_expenses_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);
```

2. In `backend/src/services/export_service.c`:
   - Add `#include "infrastructure/repositories/account_permission_repo_impl.h"`.
   - Implement the two helpers and replace the inline queries in `transactions_export_csv()` and `daily_expenses_export_csv()`:

```c
csilk_json_t*
export_query_transactions_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0) {
        return NULL;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT t.transaction_date, t.transaction_type, t.source_type, t.amount, "
        "t.price_per_unit, t.quantity, t.fee, t.currency, t.note, "
        "a.name as asset_name, la.name as linked_asset_name, c.name as category_name "
        "FROM transactions t "
        "LEFT JOIN assets a ON t.asset_id=a.id "
        "LEFT JOIN assets la ON t.linked_asset_id=la.id "
        "LEFT JOIN categories c ON t.category_id=c.id "
        "WHERE " MF_LEDGER_SHARED_ACCESS_SQL("t") " "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL("la") " "
        "ORDER BY t.transaction_date DESC, t.id DESC";
    const char* params[] = {
        lid_str, uid_str, uid_str, uid_str, uid_str, uid_str, uid_str, uid_str, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}

csilk_json_t*
export_query_daily_expenses_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0) {
        return NULL;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT de.expense_date, de.expense_type, de.amount, de.currency, de.note, "
        "a.name as asset_name, c.name as category_name "
        "FROM daily_expenses de "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "LEFT JOIN categories c ON de.category_id=c.id "
        "WHERE " MF_LEDGER_SHARED_ACCESS_SQL("de") " "
        "AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " "
        "ORDER BY de.expense_date DESC, de.id DESC";
    const char* params[] = {lid_str, uid_str, uid_str, uid_str, uid_str, uid_str, NULL};
    return csilk_db_query_param_json(pool, sql, params);
}
```

And in `transactions_export_csv`:
```c
    csilk_json_t* rows = export_query_transactions_rows(db_get_pool(), user_id, ledger_id);
```
And in `daily_expenses_export_csv`:
```c
    csilk_json_t* rows = export_query_daily_expenses_rows(db_get_pool(), user_id, ledger_id);
```

- [ ] **Step 4: Build and run `test_account_rbac`**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R test_account_rbac --output-on-failure`
Expected: `1/1 Test #... test_account_rbac ... Passed`.

- [ ] **Step 5: Commit Task 2**

```bash
git add backend/src/services/export_service.h \
        backend/src/services/export_service.c \
        backend/tests/unit/test_account_rbac.c
git commit -m "fix(export): 🐛 enforce shared ledger scope and account RBAC in CSV export"
```

---

### Task 3: Harden CSV Import with Ledger Isolation, Anti-Enumeration, RBAC Write Guards & 10MB Cap (`import_service`)

**Files:**
- Modify: `backend/src/services/import_service.h:1-20`
- Modify: `backend/src/services/import_service.c:1-25,164-215,218-707`
- Test: `backend/tests/unit/test_account_rbac.c`

- [ ] **Step 1: Write failing test in `backend/tests/unit/test_account_rbac.c`**

Include `"services/import_service.h"` and `"infrastructure/repositories/ledger_snapshot_repo_impl.h"` at top of `test_account_rbac.c`, add `test_csv_import_rbac_and_isolation(pool)` and call it in `main()`:

```c
static void
test_csv_import_rbac_and_isolation(csilk_db_pool_t* pool)
{
    printf("--- Testing CSV Import Ledger Isolation, Anti-Enumeration & RBAC ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "imp_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "imp_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* l1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ImpL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(l1, 0), "id");
    csilk_json_free(l1);

    csilk_json_t* l2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'ImpL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(l2, 0), "id");
    csilk_json_free(l2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_mid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'ImpCash', 'asset', 'cash'), (?, ?, 'ImpCash', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* c1 = csilk_db_query_param_json(
        pool, "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ImpCash';",
        (const char*[]){s_oid, s_l1, NULL});
    int64_t cat1 = db_get_int(csilk_json_array_get(c1, 0), "id");
    csilk_json_free(c1);
    csilk_json_t* c2 = csilk_db_query_param_json(
        pool, "SELECT id FROM categories WHERE user_id=? AND ledger_id=? AND name='ImpCash';",
        (const char*[]){s_oid, s_l2, NULL});
    int64_t cat2 = db_get_int(csilk_json_array_get(c2, 0), "id");
    csilk_json_free(c2);

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type) VALUES (?, ?, 'ImpFood', 'expense');",
        (const char*[]){s_oid, s_l1, NULL});

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_l1_op = {.user_id = owner_id, .ledger_id = lid1, .name = "L1Op", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l1_vw = {.user_id = owner_id, .ledger_id = lid1, .name = "L1Vw", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l1_no = {.user_id = owner_id, .ledger_id = lid1, .name = "L1No", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l2_op = {.user_id = owner_id, .ledger_id = lid2, .name = "L2Op", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_l1_op, &acres) == 0); int64_t acct_op = acres.id;
    assert(account_usecase_create(pool, &ac_l1_vw, &acres) == 0); int64_t acct_vw = acres.id;
    assert(account_usecase_create(pool, &ac_l1_no, &acres) == 0); int64_t acct_no = acres.id;
    assert(account_usecase_create(pool, &ac_l2_op, &acres) == 0); int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_l1_op = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_op, .name = "SharedWallet", .current_value = 1000.0, .currency = "CNY"};
    create_asset_cmd_t as_l2_dup = {.user_id = owner_id, .ledger_id = lid2, .category_id = cat2, .account_id = acct_l2, .name = "SameNameInL2", .current_value = 5000.0, .currency = "CNY"};
    create_asset_cmd_t as_l1_vw = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_vw, .name = "ReadonlyWallet", .current_value = 2000.0, .currency = "CNY"};
    create_asset_cmd_t as_l1_no = {.user_id = owner_id, .ledger_id = lid1, .category_id = cat1, .account_id = acct_no, .name = "HiddenSecret", .current_value = 9999.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_l1_op, &ares) == 0); int64_t asset_op = ares.id;
    assert(asset_usecase_create(pool, &as_l2_dup, &ares) == 0); int64_t asset_l2 = ares.id;
    assert(asset_usecase_create(pool, &as_l1_vw, &ares) == 0); int64_t asset_vw = ares.id;
    assert(asset_usecase_create(pool, &as_l1_no, &ares) == 0);

    // Permissions on lid1 for member_id: operator on acct_op, viewer on acct_vw, none on acct_no
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_op, member_id, "operator", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_vw, member_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_no, member_id, "none", 0, 0, &acres) == 0);

    // Seed future snapshot at 2026-11-25 on asset_op to verify backdated CSV expense invalidates it
    ledger_position_state_t dummy_pos = {0};
    money_t zero_m = money_zero(CURRENCY_CNY);
    assert(ledger_snapshot_save(pool, owner_id, asset_op, "2026-11-25", &dummy_pos, zero_m, "2026-11-25", 1, "[]") == 0);

    // 1. Transactions CSV import by member into lid1:
    //   row1: SharedWallet valid -> OK
    //   row2: SameNameInL2 (belongs to lid2, even for owner!) -> fail ("找不到资产")
    //   row3: HiddenSecret (none) -> fail ("找不到资产", NOT "无权限")
    //   row4: ReadonlyWallet (viewer) -> fail ("账户无记账权限")
    //   row5: SharedWallet with unknown linked_asset_name -> fail ("找不到关联资产")
    const char* tx_csv =
        "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,fee,currency,linked_asset_name,note\n"
        "2026-11-20,SharedWallet,ImpFood,income,income,100,0,0,0,CNY,,ok_row\n"
        "2026-11-20,SameNameInL2,ImpFood,income,income,50,0,0,0,CNY,,cross_ledger\n"
        "2026-11-20,HiddenSecret,ImpFood,income,income,50,0,0,0,CNY,,hidden_probe\n"
        "2026-11-20,ReadonlyWallet,ImpFood,income,income,50,0,0,0,CNY,,viewer_blocked\n"
        "2026-11-20,SharedWallet,ImpFood,income,income,50,0,0,0,CNY,NoSuchLinked,bad_link\n";
    char* out_json = NULL;
    char  err_buf[256] = {0};
    assert(import_transactions_csv_execute(pool, 0, member_id, lid1, tx_csv, strlen(tx_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    assert(out_json != NULL);
    csilk_json_t* res_obj = csilk_json_parse(out_json);
    assert(res_obj != NULL);
    assert(db_get_int(res_obj, "imported") == 1);
    assert(db_get_int(res_obj, "errors") == 4);
    const char* details = csilk_json_get_string(res_obj, "errors_detail");
    assert(details != NULL);
    assert(strstr(details, "找不到资产 'SameNameInL2'") != NULL);
    assert(strstr(details, "找不到资产 'HiddenSecret'") != NULL);
    assert(strstr(details, "账户无记账权限") != NULL);
    assert(strstr(details, "找不到关联资产 'NoSuchLinked'") != NULL);
    csilk_json_free(res_obj);
    free(out_json);

    // Owner tries importing into lid1 referencing lid2's SameNameInL2 -> must ALSO fail (no cross-ledger hijack)
    const char* owner_cross_csv =
        "date,asset_name,category_name,transaction_type,source_type,amount,price_per_unit,quantity,fee,currency,linked_asset_name,note\n"
        "2026-11-20,SameNameInL2,ImpFood,income,income,50,0,0,0,CNY,,cross_ledger\n";
    out_json = NULL;
    assert(import_transactions_csv_execute(pool, 0, owner_id, lid1, owner_cross_csv, strlen(owner_cross_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    res_obj = csilk_json_parse(out_json);
    assert(db_get_int(res_obj, "imported") == 0 && db_get_int(res_obj, "errors") == 1);
    csilk_json_free(res_obj);
    free(out_json);

    // Verify lid2 asset balance stayed 5000.0 untouched
    char s_al2[32];
    snprintf(s_al2, sizeof(s_al2), "%lld", (long long)asset_l2);
    csilk_json_t* chk_l2 = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){s_al2, NULL});
    assert(db_get_num(csilk_json_array_get(chk_l2, 0), "current_value") == 5000.0);
    csilk_json_free(chk_l2);

    // 2. Daily expenses CSV import: valid row invalidates backdated snapshot; viewer/none rejected
    const char* de_csv =
        "date,asset_name,category_name,expense_type,amount,currency,note\n"
        "2026-11-20,SharedWallet,ImpFood,expense,30,CNY,ok_exp\n"
        "2026-11-20,ReadonlyWallet,ImpFood,expense,40,CNY,vw_exp\n"
        "2026-11-20,HiddenSecret,ImpFood,expense,50,CNY,no_exp\n";
    out_json = NULL;
    assert(import_daily_expenses_csv_execute(pool, 0, member_id, lid1, de_csv, strlen(de_csv), &out_json, err_buf, sizeof(err_buf)) == 0);
    res_obj = csilk_json_parse(out_json);
    assert(db_get_int(res_obj, "imported") == 1);
    assert(db_get_int(res_obj, "errors") == 2);
    details = csilk_json_get_string(res_obj, "errors_detail");
    assert(strstr(details, "找不到资产 'HiddenSecret'") != NULL);
    assert(strstr(details, "账户无记账权限") != NULL);
    csilk_json_free(res_obj);
    free(out_json);

    // Snapshot on 2026-11-25 must have been invalidated by 2026-11-20 daily expense import
    csilk_json_t* snap = NULL;
    assert(ledger_snapshot_get_latest(pool, owner_id, asset_op, NULL, &snap) == 0 && snap == NULL);

    // ReadonlyWallet balance untouched (2000.0)
    char s_avw[32];
    snprintf(s_avw, sizeof(s_avw), "%lld", (long long)asset_vw);
    csilk_json_t* chk_vw = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=?", (const char*[]){s_avw, NULL});
    assert(db_get_num(csilk_json_array_get(chk_vw, 0), "current_value") == 2000.0);
    csilk_json_free(chk_vw);
    printf("  [OK] CSV import enforces ledger isolation, anti-enumeration, RBAC write guard & snapshot invalidation\n");
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run: `cmake --build backend/build --target test_account_rbac`
Expected: FAIL with implicit declaration of `import_transactions_csv_execute` / `import_daily_expenses_csv_execute`.

- [ ] **Step 3: Implement CSV import hardening in `import_service.h` & `import_service.c`**

1. In `backend/src/services/import_service.h`, include `"common/db.h"`, `<stddef.h>`, `<stdint.h>` and add:

```c
#define MF_MAX_CSV_IMPORT_BYTES ((size_t)(10 * 1024 * 1024))

int import_transactions_csv_execute(csilk_db_pool_t* pool,
                                    int64_t          task_id,
                                    int64_t          user_id,
                                    int64_t          ledger_id,
                                    const char*      csv_data,
                                    size_t           csv_len,
                                    char**           out_result_json,
                                    char*            err_buf,
                                    size_t           err_cap);

int import_daily_expenses_csv_execute(csilk_db_pool_t* pool,
                                      int64_t          task_id,
                                      int64_t          user_id,
                                      int64_t          ledger_id,
                                      const char*      csv_data,
                                      size_t           csv_len,
                                      char**           out_result_json,
                                      char*            err_buf,
                                      size_t           err_cap);
```

2. In `backend/src/services/import_service.c`:
   - Add `#include "infrastructure/repositories/account_permission_repo_impl.h"` and `#include "application/account/usecases.h"`.
   - Add helpers `resolve_import_asset`, `resolve_import_category`, and `category_belongs_to_ledger`:

```c
static int
resolve_import_asset(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          ledger_id,
                     const char*      name,
                     int64_t*         out_asset_id,
                     int64_t*         out_account_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !name || !name[0]) {
        return -1;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT a.id, COALESCE(a.account_id, 0) AS account_id "
        "FROM assets a "
        "WHERE a.name = ? "
        "AND " MF_LEDGER_SHARED_ACCESS_SQL("a") " "
        "AND " MF_RBAC_ASSET_VISIBLE_SQL("a") " "
        "ORDER BY a.id ASC LIMIT 1";
    const char* params[] = {name, lid_str, uid_str, uid_str, uid_str, uid_str, uid_str, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    if (!res || csilk_json_array_size(res) == 0) {
        if (res) {
            csilk_json_free(res);
        }
        return -1;
    }
    csilk_json_t* row = csilk_json_array_get(res, 0);
    if (out_asset_id) {
        *out_asset_id = db_get_int(row, "id");
    }
    if (out_account_id) {
        *out_account_id = db_get_int(row, "account_id");
    }
    csilk_json_free(res);
    return (out_asset_id && *out_asset_id > 0) ? 0 : -1;
}

static int64_t
resolve_import_category(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id, const char* name)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || !name || !name[0]) {
        return 0;
    }
    char uid_str[32], lid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char* sql =
        "SELECT c.id FROM categories c "
        "WHERE c.name = ? AND " MF_LEDGER_SHARED_ACCESS_SQL("c") " "
        "ORDER BY c.id ASC LIMIT 1";
    const char*   params[] = {name, lid_str, uid_str, uid_str, uid_str, NULL};
    csilk_json_t* res = csilk_db_query_param_json(pool, sql, params);
    int64_t       cid = 0;
    if (res && csilk_json_array_size(res) > 0) {
        cid = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return cid;
}

static bool
category_belongs_to_ledger(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id, int64_t cat_id)
{
    if (!pool || user_id <= 0 || ledger_id <= 0 || cat_id <= 0) {
        return false;
    }
    char cid_str[32], uid_str[32], lid_str[32];
    snprintf(cid_str, sizeof(cid_str), "%lld", (long long)cat_id);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(lid_str, sizeof(lid_str), "%lld", (long long)ledger_id);

    const char*   sql = "SELECT c.id FROM categories c WHERE c.id = ? AND " MF_LEDGER_SHARED_ACCESS_SQL("c") " LIMIT 1";
    csilk_json_t* res = csilk_db_query_param_json(
        pool, sql, (const char*[]){cid_str, lid_str, uid_str, uid_str, uid_str, NULL});
    bool ok = res && csilk_json_array_size(res) > 0;
    if (res) {
        csilk_json_free(res);
    }
    return ok;
}
```

   - In `submit_csv_import_task` (after BOM strip & `csv_len == 0` check):

```c
    if (csv_len > MF_MAX_CSV_IMPORT_BYTES) {
        respond_bad_request(c, "CSV 文件大小不能超过 10MB");
        return;
    }
```

   - Extract `import_transactions_csv_execute()` and `import_daily_expenses_csv_execute()` (creating a mutable copy or scanning line offsets without mutating `const char* csv_data` via `arg` / `strdup` — easiest: pass `char* mutable = malloc(csv_len + 1)` inside `*_execute` or guard `task_id > 0` before calling `mf_async_task_update_progress(pool, task_id, ...)`!):
     - Resolve `asset_id` & `account_id` via `resolve_import_asset(pool, user_id, ledger_id, asset_name, &asset_id, &account_id)`; on failure log `第%d行: 找不到资产 '%s'\n`.
     - Check `account_usecase_check_permission(pool, ledger_id, user_id, account_id, MF_ACCOUNT_ACTION_WRITE_TX)` when `account_id > 0`; on non-zero log `第%d行: 账户无记账权限\n` and `continue`.
     - Resolve `category_id` via `resolve_import_category(pool, user_id, ledger_id, cat_name)`; if `<= 0`, run `apply_smart_rules(...)` and validate with `category_belongs_to_ledger(...)` (reset `category_id = 0` if invalid for `ledger_id`).
     - In transactions import, when `linked_s[0]` is non-empty: call `resolve_import_asset(pool, user_id, ledger_id, linked_s, &linked_id, &linked_account_id)`; if `< 0` log `第%d行: 找不到关联资产 '%s'\n` and `continue`; if `linked_account_id > 0` check `MF_ACCOUNT_ACTION_WRITE_TX` and on non-zero log `第%d行: 关联账户无记账权限\n` and `continue`.
     - In daily expenses import, replace `ledger_apply_expense(...)` with `ledger_apply_expense_dated(pool, user_id, asset_id, amt_m, is_income, expense_id, note_s ? note_s : "", date_s)`.
     - Guard `if (task_id > 0) { mf_async_task_update_progress(...); }` so synchronous calls from unit tests with `task_id = 0` work cleanly.
     - Have `transactions_import_csv_task_fn` and `daily_expenses_import_csv_task_fn` delegate directly to `import_transactions_csv_execute` and `import_daily_expenses_csv_execute`.

- [ ] **Step 4: Build and run `test_account_rbac`**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R test_account_rbac --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit Task 3**

```bash
git add backend/src/services/import_service.h \
        backend/src/services/import_service.c \
        backend/tests/unit/test_account_rbac.c
git commit -m "fix(import): 🐛 enforce ledger isolation, RBAC write guards, and size cap in CSV import"
```

---

### Task 4: Add Fallback Shared-Ledger Macro & Align All 9 Portfolio Repository Queries

**Files:**
- Modify: `backend/src/infrastructure/repositories/account_permission_repo_impl.h:36-53`
- Modify: `backend/src/infrastructure/repositories/portfolio_repo_impl.c:1-585`
- Test: `backend/tests/unit/test_account_rbac.c`

- [ ] **Step 1: Write failing test in `backend/tests/unit/test_account_rbac.c`**

Add `test_portfolio_shared_ledger_rbac(pool)` and call it from `main()`:

```c
static void
test_portfolio_shared_ledger_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing Portfolio Repository Shared Ledger + RBAC Alignment ---\n");
    int64_t owner_id = 0, member_id = 0;
    assert(mf_auth_repo_create(pool, "port_owner", "hash1", &owner_id) == 0 && owner_id > 0);
    assert(mf_auth_repo_create(pool, "port_member", "hash2", &member_id) == 0 && member_id > 0);

    char s_oid[32], s_mid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_mid, sizeof(s_mid), "%lld", (long long)member_id);

    csilk_json_t* lrow = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'PortShared', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t ledger_id = db_get_int(csilk_json_array_get(lrow, 0), "id");
    csilk_json_free(lrow);

    char s_lid[32];
    snprintf(s_lid, sizeof(s_lid), "%lld", (long long)ledger_id);
    csilk_db_exec_param(
        pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'viewer');",
        (const char*[]){s_lid, s_oid, s_lid, s_mid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'PortCash', 'asset', 'cash'), "
        "(?, ?, 'PortStock', 'asset', 'stock'), "
        "(?, ?, 'PortLoan', 'asset', 'loan');",
        (const char*[]){s_oid, s_lid, s_oid, s_lid, s_oid, s_lid, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, asset_type FROM categories WHERE user_id=? AND ledger_id=?;",
        (const char*[]){s_oid, s_lid, NULL});
    int64_t cat_cash = 0, cat_stock = 0, cat_loan = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        const char*   at = csilk_json_get_string(r, "asset_type");
        if (strcmp(at, "cash") == 0) cat_cash = db_get_int(r, "id");
        if (strcmp(at, "stock") == 0) cat_stock = db_get_int(r, "id");
        if (strcmp(at, "loan") == 0) cat_loan = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = ledger_id, .name = "PortOpen", .institution = "broker", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = ledger_id, .name = "PortMask", .institution = "broker", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0); int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0); int64_t acct_mask = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t c_cash = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_cash, .account_id = acct_open, .name = "OwnerCash", .current_value = 10000.0, .currency = "CNY"};
    create_asset_cmd_t c_stk = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_stock, .account_id = acct_open, .name = "OwnerStock", .current_value = 0.0, .quantity = 0.0, .cost_basis = 0.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t c_loan = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_loan, .account_id = acct_open, .name = "OwnerLoan", .current_value = 500.0, .currency = "CNY"};
    create_asset_cmd_t c_mstk = {.user_id = owner_id, .ledger_id = ledger_id, .category_id = cat_stock, .account_id = acct_mask, .name = "OwnerMaskStock", .current_value = 9000.0, .quantity = 90.0, .cost_basis = 8000.0, .net_value = 100.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &c_cash, &ares) == 0); int64_t asset_cash = ares.id;
    assert(asset_usecase_create(pool, &c_stk, &ares) == 0); int64_t asset_stk = ares.id;
    assert(asset_usecase_create(pool, &c_loan, &ares) == 0);
    assert(asset_usecase_create(pool, &c_mstk, &ares) == 0);

    tx_usecase_result_t txres = {0};
    create_tx_cmd_t buy_cmd = {
        .user_id = owner_id, .ledger_id = ledger_id, .asset_id = asset_stk, .linked_asset_id = asset_cash,
        .category_id = cat_stock, .type = "buy", .amount = 1000.0, .price_per_unit = 10.0, .quantity = 100.0,
        .currency = "CNY", .date = "2026-11-22", .note = "owner_buy"
    };
    assert(tx_usecase_create(pool, &buy_cmd, &txres) == 0);

    // Restrict member on acct_mask (mask_balance=1); acct_open remains unmasked viewer
    assert(account_permission_usecase_set(pool, owner_id, ledger_id, acct_mask, member_id, "viewer", 1, 0, &acres) == 0);

    mf_holding_item_t* items = NULL; size_t hcount = 0;
    assert(mf_portfolio_repo_get_holdings(pool, member_id, ledger_id, &items, &hcount) == 0);
    assert(hcount == 1 && items[0].asset_id == asset_stk);
    mf_portfolio_repo_free_holdings(items, hcount);

    mf_portfolio_trade_event_t* evs = NULL; size_t ecount = 0;
    assert(mf_portfolio_repo_get_trade_events(pool, member_id, ledger_id, &evs, &ecount) == 0);
    assert(ecount == 1 && evs[0].asset_id == asset_stk);
    mf_portfolio_repo_free_trade_events(evs, ecount);

    csilk_json_t* cat_assets = portfolio_repo_get_category_assets(pool, member_id, ledger_id);
    assert(cat_assets && csilk_json_array_size(cat_assets) == 2); // OwnerCash + OwnerStock, OwnerMaskStock excluded
    csilk_json_free(cat_assets);

    csilk_json_t* liabs = portfolio_repo_get_liability_assets(pool, member_id, ledger_id);
    assert(liabs && csilk_json_array_size(liabs) == 1);
    csilk_json_free(liabs);

    csilk_json_t* rec = portfolio_repo_get_recent_transactions(pool, member_id, ledger_id, 10);
    assert(rec && csilk_json_array_size(rec) == 1);
    csilk_json_free(rec);

    csilk_json_t* trend_a = portfolio_repo_get_trend_assets(pool, member_id, ledger_id);
    assert(trend_a && csilk_json_array_size(trend_a) == 3);
    csilk_json_free(trend_a);

    csilk_json_t* trend_l = portfolio_repo_get_trend_balance_logs(pool, member_id, ledger_id, "2026-11-01");
    assert(trend_l && csilk_json_array_size(trend_l) >= 1);
    csilk_json_free(trend_l);

    csilk_json_t* perf_t = portfolio_repo_get_performance_transactions(pool, member_id, ledger_id);
    assert(perf_t && csilk_json_array_size(perf_t) == 1);
    csilk_json_free(perf_t);

    csilk_json_t* hraw = portfolio_repo_get_holdings_raw(pool, member_id, ledger_id);
    assert(hraw && csilk_json_array_size(hraw) == 1);
    csilk_json_free(hraw);
    printf("  [OK] All 9 portfolio repo queries support shared ledger members + RBAC exclusion\n");
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R test_account_rbac --output-on-failure`
Expected: FAIL on `assert(hcount == 1 ...)` because `portfolio_repo_impl.c` currently filters `a.user_id = ?`.

- [ ] **Step 3: Add fallback macro in `account_permission_repo_impl.h` & update all 9 queries in `portfolio_repo_impl.c`**

1. In `backend/src/infrastructure/repositories/account_permission_repo_impl.h` (after `MF_LEDGER_SHARED_BALANCE_LOG_SQL`):

```c
/**
 * @brief 兼容历史 NULL/0 ledger_id 行的共享账本读谓词（回退至关联资产 ledger_id）
 *        需要绑定 4 个参数：[ledger_id, user_id, user_id, user_id]
 */
#define MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL(alias, asset_alias)                                   \
    "(COALESCE(NULLIF(" alias ".ledger_id, 0), " asset_alias ".ledger_id) = ? AND ("               \
    alias ".user_id = ? OR EXISTS (SELECT 1 FROM ledgers _l WHERE _l.id = "                        \
    "COALESCE(NULLIF(" alias ".ledger_id, 0), " asset_alias ".ledger_id) AND _l.owner_id = ?) "     \
    "OR EXISTS (SELECT 1 FROM ledger_members _lm WHERE _lm.ledger_id = "                          \
    "COALESCE(NULLIF(" alias ".ledger_id, 0), " asset_alias ".ledger_id) AND _lm.user_id = ?)))"

/**
 * @brief 兼容历史 NULL/0 ledger_id 行的共享账本写谓词（创建者或账本 owner/editor 可改删）
 *        需要绑定 3 个参数：[user_id, user_id, user_id]
 */
#define MF_LEDGER_SHARED_WRITE_FALLBACK_SQL(tbl, asset_subquery_col)                               \
    "(" tbl ".user_id = ? OR EXISTS (SELECT 1 FROM ledgers _l WHERE _l.id = "                      \
    "COALESCE(NULLIF(" tbl ".ledger_id, 0), (SELECT ledger_id FROM assets WHERE id = "             \
    tbl "." asset_subquery_col ")) AND _l.owner_id = ?) "                                          \
    "OR EXISTS (SELECT 1 FROM ledger_members _lm WHERE _lm.ledger_id = "                          \
    "COALESCE(NULLIF(" tbl ".ledger_id, 0), (SELECT ledger_id FROM assets WHERE id = "             \
    tbl "." asset_subquery_col ")) AND _lm.user_id = ? AND _lm.role IN ('owner', 'editor')))"
```

2. In `backend/src/infrastructure/repositories/portfolio_repo_impl.c`, update the `ledger_id > 0` branch of all 9 functions (increasing `sql` buffer to `2048` and `params` array to `[16]` / `[24]` where needed, binding `[lid_str, uid_str, uid_str, uid_str]` for shared access and `[uid_str, uid_str]` for each `MF_RBAC_ASSET_*` macro):
   - `mf_portfolio_repo_get_holdings`: `WHERE " MF_LEDGER_SHARED_ACCESS_SQL("a") " AND c.asset_type IN ('stock','fund','bond','crypto') AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " ORDER BY a.id ASC` (params: `lid, uid, uid, uid, uid, uid`)
   - `mf_portfolio_repo_get_trade_events`: `WHERE " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("t", "a") " AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " ORDER BY t.transaction_date ASC, t.id ASC` (params: `lid, uid, uid, uid, uid, uid`)
   - `portfolio_repo_get_category_assets`: `WHERE " MF_LEDGER_SHARED_ACCESS_SQL("a") " AND c.asset_type NOT IN ('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL("a")`
   - `portfolio_repo_get_liability_assets`: `WHERE " MF_LEDGER_SHARED_ACCESS_SQL("a") " AND c.asset_type IN ('loan','credit_card','other_liability') AND " MF_RBAC_ASSET_UNMASKED_SQL("a")`
   - `portfolio_repo_get_recent_transactions`: `WHERE " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("t", "a") " AND " MF_RBAC_ASSET_VISIBLE_SQL("a") " AND " MF_RBAC_ASSET_VISIBLE_SQL("la") " ORDER BY t.transaction_date DESC, t.id DESC LIMIT ?` (params: 4 for 2 `MASK_BALANCE_COL`, 4 for `SHARED_ACCESS_FALLBACK`, 4 for 2 `VISIBLE_SQL`, plus `lim_str` = 13 total)
   - `portfolio_repo_get_trend_assets`: both Postgres and SQLite `ledger_id > 0` branches use `WHERE " MF_LEDGER_SHARED_ACCESS_SQL("a") " AND " MF_RBAC_ASSET_UNMASKED_SQL("a")` (params `[10]`: `lid, uid, uid, uid, uid, uid`)
   - `portfolio_repo_get_trend_balance_logs`: both Postgres and SQLite `ledger_id > 0` branches use `WHERE " MF_LEDGER_SHARED_BALANCE_LOG_SQL("abl", "a") " AND abl.created_at::date >= ?::date AND " MF_RBAC_ASSET_UNMASKED_SQL("a")` (and `date(abl.created_at) >= ?` for SQLite; params `[12]`: `lid, uid, uid, uid, start_date, uid, uid`)
   - `portfolio_repo_get_performance_transactions`: `WHERE " MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("t", "a") " AND (t.parent_tx_id IS NULL OR t.parent_tx_id = 0) AND " MF_RBAC_ASSET_UNMASKED_SQL("a") " ORDER BY t.transaction_date ASC, t.id ASC`
   - `portfolio_repo_get_holdings_raw`: `WHERE " MF_LEDGER_SHARED_ACCESS_SQL("a") " AND c.asset_type IN ('stock','fund','bond','crypto') AND " MF_RBAC_ASSET_UNMASKED_SQL("a")`

- [ ] **Step 4: Build and run `test_account_rbac` & `test_domain_repositories`**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R "test_account_rbac|test_domain_repositories" --output-on-failure`
Expected: Both PASS.

- [ ] **Step 5: Commit Task 4**

```bash
git add backend/src/infrastructure/repositories/account_permission_repo_impl.h \
        backend/src/infrastructure/repositories/portfolio_repo_impl.c \
        backend/tests/unit/test_account_rbac.c
git commit -m "fix(portfolio): 🐛 enable shared ledger visibility across all portfolio queries"
```

---

### Task 5: Align DCA Full Stack with Ledger Isolation, Shared Access & Account RBAC

**Files:**
- Modify: `backend/src/domain/dca/entity.h:14-31`
- Modify: `backend/src/domain/dca/repository.h:1-96`
- Modify: `backend/src/application/dca/commands.h:1-56`
- Modify: `backend/src/application/dca/usecases.h:1-78`
- Modify: `backend/src/application/dca/usecases.c:1-363`
- Modify: `backend/src/infrastructure/repositories/dca_repo_impl.c:1-458`
- Modify: `backend/src/interfaces/http/controllers/dca_controller.c:18-363`
- Test: `backend/tests/unit/test_account_rbac.c`

- [ ] **Step 1: Write failing test in `backend/tests/unit/test_account_rbac.c`**

Include `"application/dca/usecases.h"` and `"infrastructure/repositories/dca_repo_impl.h"` at top of `test_account_rbac.c`, add `test_dca_shared_ledger_and_rbac(pool)` and call it from `main()`:

```c
static void
test_dca_shared_ledger_and_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing DCA Ledger Isolation, Shared Access & Account RBAC ---\n");
    int64_t owner_id = 0, editor_id = 0, viewer_id = 0;
    assert(mf_auth_repo_create(pool, "dca_owner", "h1", &owner_id) == 0);
    assert(mf_auth_repo_create(pool, "dca_editor", "h2", &editor_id) == 0);
    assert(mf_auth_repo_create(pool, "dca_viewer", "h3", &viewer_id) == 0);

    char s_oid[32], s_eid[32], s_vid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_eid, sizeof(s_eid), "%lld", (long long)editor_id);
    snprintf(s_vid, sizeof(s_vid), "%lld", (long long)viewer_id);

    csilk_json_t* lr1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'DcaL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(lr1, 0), "id");
    csilk_json_free(lr1);
    csilk_json_t* lr2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'DcaL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(lr2, 0), "id");
    csilk_json_free(lr2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool,
        "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'viewer'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_eid, s_l1, s_vid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool,
        "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES "
        "(?, ?, 'DcaCash1', 'asset', 'cash'), (?, ?, 'DcaFund1', 'asset', 'fund'), (?, ?, 'DcaCash2', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, name FROM categories WHERE user_id=?;", (const char*[]){s_oid, NULL});
    int64_t c_cash1 = 0, c_fund1 = 0, c_cash2 = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        const char*   nm = csilk_json_get_string(r, "name");
        if (strcmp(nm, "DcaCash1") == 0) c_cash1 = db_get_int(r, "id");
        if (strcmp(nm, "DcaFund1") == 0) c_fund1 = db_get_int(r, "id");
        if (strcmp(nm, "DcaCash2") == 0) c_cash2 = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_open = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaOpen", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_mask = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaMask", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_none = {.user_id = owner_id, .ledger_id = lid1, .name = "DcaNone", .institution = "fund_platform", .currency = "CNY"};
    create_account_cmd_t ac_l2   = {.user_id = owner_id, .ledger_id = lid2, .name = "DcaL2", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_open, &acres) == 0); int64_t acct_open = acres.id;
    assert(account_usecase_create(pool, &ac_mask, &acres) == 0); int64_t acct_mask = acres.id;
    assert(account_usecase_create(pool, &ac_none, &acres) == 0); int64_t acct_none = acres.id;
    assert(account_usecase_create(pool, &ac_l2, &acres) == 0);   int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t a_cash1 = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_cash1, .account_id = acct_open, .name = "DcaCashA", .current_value = 5000.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_op = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_open, .name = "DcaFundOpen", .current_value = 1000.0, .quantity = 100.0, .cost_basis = 900.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_mk = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_mask, .name = "DcaFundMask", .current_value = 8000.0, .quantity = 800.0, .cost_basis = 7000.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_fund_no = {.user_id = owner_id, .ledger_id = lid1, .category_id = c_fund1, .account_id = acct_none, .name = "DcaFundNone", .current_value = 9000.0, .quantity = 900.0, .cost_basis = 8000.0, .net_value = 10.0, .currency = "CNY"};
    create_asset_cmd_t a_cash2 = {.user_id = owner_id, .ledger_id = lid2, .category_id = c_cash2, .account_id = acct_l2, .name = "DcaCashL2", .current_value = 5000.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &a_cash1, &ares) == 0);   int64_t cash1 = ares.id;
    assert(asset_usecase_create(pool, &a_fund_op, &ares) == 0); int64_t fund_op = ares.id;
    assert(asset_usecase_create(pool, &a_fund_mk, &ares) == 0); int64_t fund_mk = ares.id;
    assert(asset_usecase_create(pool, &a_fund_no, &ares) == 0); int64_t fund_no = ares.id;
    assert(asset_usecase_create(pool, &a_cash2, &ares) == 0);   int64_t cash2 = ares.id;

    // Cross-ledger asset pairing rejected with 1004
    dca_usecase_result_t dres = {0};
    create_dca_plan_cmd_t bad_cross = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_op, .funding_asset_id = cash2, .name = "Cross", .amount = 100.0};
    assert(dca_usecase_create_plan(pool, &bad_cross, &dres) < 0 && dres.code == 1004);

    // Owner creates 3 plans in lid1
    create_dca_plan_cmd_t p_op = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_op, .funding_asset_id = cash1, .name = "PlanOpen", .amount = 100.0};
    create_dca_plan_cmd_t p_mk = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_mk, .funding_asset_id = cash1, .name = "PlanMask", .amount = 200.0};
    create_dca_plan_cmd_t p_no = {.user_id = owner_id, .ledger_id = lid1, .target_asset_id = fund_no, .funding_asset_id = cash1, .name = "PlanNone", .amount = 300.0};
    int64_t pid_op = dca_usecase_create_plan(pool, &p_op, &dres); assert(pid_op > 0);
    int64_t pid_mk = dca_usecase_create_plan(pool, &p_mk, &dres); assert(pid_mk > 0);
    int64_t pid_no = dca_usecase_create_plan(pool, &p_no, &dres); assert(pid_no > 0);

    // Simulate legacy NULL ledger_id row on pid_op to prove fallback compatibility
    char s_pop[32];
    snprintf(s_pop, sizeof(s_pop), "%lld", (long long)pid_op);
    csilk_db_exec_param(pool, "UPDATE dca_plans SET ledger_id = NULL WHERE id = ?;", (const char*[]){s_pop, NULL});

    // Grant editor_id: viewer+mask on acct_mask, none on acct_none, viewer on acct_open
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_open, editor_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_mask, editor_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_none, editor_id, "none", 0, 0, &acres) == 0);

    int64_t total = 0;
    csilk_json_t* list = dca_usecase_list_plans(pool, editor_id, lid1, 1, 20, NULL, &total);
    assert(list != NULL && total == 2 && csilk_json_array_size(list) == 2); // pid_no hidden
    for (size_t i = 0; i < csilk_json_array_size(list); i++) {
        csilk_json_t* it = csilk_json_array_get(list, i);
        if (db_get_int(it, "id") == pid_mk) {
            assert(csilk_json_get_bool(it, "is_balance_masked") == true);
            assert(db_get_num(it, "target_current_value") == 0.0);
            assert(db_get_num(it, "target_cost_basis") == 0.0);
        }
    }
    csilk_json_free(list);

    // Summary excludes both masked and none (only PlanOpen = 1000.0)
    csilk_json_t* sum = dca_usecase_plan_summary(pool, editor_id, lid1);
    assert(sum != NULL && db_get_num(sum, "total_current_value") == 1000.0);
    csilk_json_free(sum);

    // Create pending execution on pid_op, verify editor_id with viewer account perm is blocked 1004 on confirm & skip & delete
    int64_t ex_id = mf_dca_repo_execution_create(pool, pid_op, owner_id, "2026-11-22", 100.0);
    assert(ex_id > 0);
    confirm_dca_execution_cmd_t ccmd = {.user_id = editor_id, .ledger_id = lid1, .exec_id = ex_id, .actual_amount = 100.0, .executed_price = 10.0};
    dca_confirm_result_t cres = {0};
    assert(dca_usecase_confirm_execution(pool, &ccmd, &cres) != 0 && cres.code == 1004);
    assert(dca_usecase_skip_execution(pool, editor_id, lid1, ex_id, &dres) != 0 && dres.code == 1004);
    assert(dca_usecase_delete_plan(pool, editor_id, lid1, pid_op, &dres) != 0 && dres.code == 1004);

    // Viewer ledger role cannot even get past controller, and in lid2 pid_op is invisible
    total = 0;
    list = dca_usecase_list_plans(pool, owner_id, lid2, 1, 20, NULL, &total);
    assert(list != NULL && total == 0);
    csilk_json_free(list);
    printf("  [OK] DCA enforces ledger isolation, NULL fallback, shared access, masking & WRITE_TX\n");
}
```

- [ ] **Step 2: Run test to verify compilation fails on missing `ledger_id` signatures**

Run: `cmake --build backend/build --target test_account_rbac`
Expected: FAIL on too many arguments to `dca_usecase_list_plans` / unknown field `ledger_id`.

- [ ] **Step 3: Update DCA Domain, Application, Repository, and Controller layers**

1. **`backend/src/domain/dca/entity.h`**: add `int64_t ledger_id;` after `user_id` in `mf_dca_plan_t`.
2. **`backend/src/domain/dca/repository.h`**: update signatures to add `int64_t ledger_id` (keep `mf_dca_repo_plan_list_all_active` and `mf_dca_repo_execution_create` unchanged):

```c
csilk_json_t* mf_dca_repo_plan_list(void*       pool,
                                    int64_t     user_id,
                                    int64_t     ledger_id,
                                    int64_t     page,
                                    int64_t     page_size,
                                    const char* status,
                                    int64_t*    out_total);
csilk_json_t* mf_dca_repo_plan_summary(void* pool, int64_t user_id, int64_t ledger_id);
csilk_json_t* mf_dca_repo_plan_get(void* pool, int64_t user_id, int64_t ledger_id, int64_t id);
int64_t mf_dca_repo_plan_create(void* pool, int64_t user_id, const mf_dca_plan_t* plan);
int mf_dca_repo_plan_update(void* pool, int64_t user_id, int64_t ledger_id, int64_t id, const mf_dca_plan_t* plan);
int mf_dca_repo_plan_set_status(void* pool, int64_t user_id, int64_t ledger_id, int64_t id, const char* status);
int mf_dca_repo_plan_delete(void* pool, int64_t user_id, int64_t ledger_id, int64_t id);
csilk_json_t* mf_dca_repo_plan_list_all_active(void* pool);
int64_t mf_dca_repo_execution_create(void* pool, int64_t plan_id, int64_t user_id, const char* period_date, double planned_amount);
csilk_json_t* mf_dca_repo_execution_list_by_plan(void* pool, int64_t user_id, int64_t ledger_id, int64_t plan_id);
csilk_json_t* mf_dca_repo_execution_list_pending(void* pool, int64_t user_id, int64_t ledger_id);
csilk_json_t* mf_dca_repo_execution_get(void* pool, int64_t user_id, int64_t ledger_id, int64_t id);
int mf_dca_repo_execution_update_confirmed(void* pool, int64_t id, double actual_amount, double executed_price, double executed_quantity, int64_t transaction_id);
int mf_dca_repo_execution_update_status(void* pool, int64_t user_id, int64_t ledger_id, int64_t id, const char* status);
```

3. **`backend/src/application/dca/commands.h`** & **`backend/src/application/dca/usecases.h`**:
   - Add `int64_t ledger_id;` to `create_dca_plan_cmd_t`, `update_dca_plan_cmd_t`, `confirm_dca_execution_cmd_t`.
   - Add `int64_t ledger_id` to `dca_usecase_list_plans`, `dca_usecase_plan_summary`, `dca_usecase_get_plan`, `dca_usecase_set_plan_status`, `dca_usecase_delete_plan`, `dca_usecase_list_executions`, `dca_usecase_list_pending`, `dca_usecase_skip_execution`.

4. **`backend/src/infrastructure/repositories/dca_repo_impl.c`**:
   - Include `"infrastructure/repositories/account_permission_repo_impl.h"`.
   - Add helper `format_dca_plan_rows(csilk_json_t* raw_rows)` (mirroring `format_recent_tx_rows` in `portfolio_repo_impl.c:285-323`) to clone each row into a mutable `csilk_json_object()`, mask `target_current_value`, `target_cost_basis`, `target_quantity` to `0.0` when `db_get_int(r, "mask_bal_ta") != 0`, attach `is_balance_masked`, and omit the internal `mask_bal_ta` column:

```c
static csilk_json_t*
format_dca_plan_rows(csilk_json_t* raw_rows)
{
    if (!raw_rows || !csilk_json_is_array(raw_rows)) {
        return raw_rows;
    }
    size_t        n = csilk_json_array_size(raw_rows);
    csilk_json_t* out = csilk_json_array();
    if (!out) {
        return raw_rows;
    }
    for (size_t i = 0; i < n; i++) {
        const csilk_json_t* r = csilk_json_array_get(raw_rows, i);
        if (!r || !csilk_json_is_object(r)) {
            continue;
        }
        bool          masked = db_get_int(r, "mask_bal_ta") != 0;
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_int(item, "id", db_get_int(r, "id"));
        csilk_json_add_int(item, "user_id", db_get_int(r, "user_id"));
        csilk_json_add_int(item, "ledger_id", db_get_int(r, "ledger_id"));
        csilk_json_add_int(item, "target_asset_id", db_get_int(r, "target_asset_id"));
        csilk_json_add_int(item, "funding_asset_id", db_get_int(r, "funding_asset_id"));
        csilk_json_add_string(item, "name", csilk_json_get_string(r, "name"));
        csilk_json_add_string(item, "frequency", csilk_json_get_string(r, "frequency"));
        csilk_json_add_int(item, "day_of_period", db_get_int(r, "day_of_period"));
        csilk_json_add_number(item, "amount", db_get_num(r, "amount"));
        csilk_json_add_number(item, "target_profit_rate", db_get_num(r, "target_profit_rate"));
        csilk_json_add_number(item, "target_total_amount", db_get_num(r, "target_total_amount"));
        csilk_json_add_int(item, "target_total_periods", db_get_int(r, "target_total_periods"));
        csilk_json_add_string(item, "status", csilk_json_get_string(r, "status"));
        csilk_json_add_string(item, "note", csilk_json_get_string(r, "note"));
        csilk_json_add_string(item, "created_at", csilk_json_get_string(r, "created_at"));
        csilk_json_add_string(item, "updated_at", csilk_json_get_string(r, "updated_at"));
        csilk_json_add_string(
            item, "target_asset_name", csilk_json_get_string(r, "target_asset_name"));
        csilk_json_add_string(item, "target_symbol", csilk_json_get_string(r, "target_symbol"));
        csilk_json_add_number(item, "target_net_value", db_get_num(r, "target_net_value"));
        csilk_json_add_number(
            item, "target_quantity", masked ? 0.0 : db_get_num(r, "target_quantity"));
        csilk_json_add_number(
            item, "target_cost_basis", masked ? 0.0 : db_get_num(r, "target_cost_basis"));
        csilk_json_add_number(
            item, "target_current_value", masked ? 0.0 : db_get_num(r, "target_current_value"));
        csilk_json_add_bool(item, "is_balance_masked", masked);
        csilk_json_add_string(
            item, "target_quote_source", csilk_json_get_string(r, "target_quote_source"));
        csilk_json_add_string(
            item, "target_currency", csilk_json_get_string(r, "target_currency"));
        csilk_json_add_string(
            item, "funding_asset_name", csilk_json_get_string(r, "funding_asset_name"));
        csilk_json_add_string(
            item, "funding_currency", csilk_json_get_string(r, "funding_currency"));
        csilk_json_add_int(item, "executed_periods", db_get_int(r, "executed_periods"));
        csilk_json_add_number(
            item, "total_invested_amount", db_get_num(r, "total_invested_amount"));
        csilk_json_array_append(out, item);
    }
    csilk_json_free(raw_rows);
    return out;
}
```

   - In `mf_dca_repo_plan_list`: `count_sql` joins `JOIN assets ta ON ta.id = p.target_asset_id JOIN assets fa ON fa.id = p.funding_asset_id`, using `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("p", "ta")` when `ledger_id > 0` (else `p.user_id = ?`) plus `AND MF_RBAC_ASSET_VISIBLE_SQL("ta") AND MF_RBAC_ASSET_VISIBLE_SQL("fa")`. Data query adds `COALESCE(NULLIF(p.ledger_id, 0), ta.ledger_id) AS ledger_id` and `MF_RBAC_MASK_BALANCE_COL("ta") AS mask_bal_ta`, uses identical `WHERE`, and wraps the returned rows with `format_dca_plan_rows(list)`.
   - In `mf_dca_repo_plan_summary`: joins `JOIN assets fa ON fa.id = p.funding_asset_id`, uses `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("p", "ta")` when `ledger_id > 0` (else `p.user_id = ?`) plus `AND MF_RBAC_ASSET_UNMASKED_SQL("ta") AND MF_RBAC_ASSET_UNMASKED_SQL("fa")`.
   - In `mf_dca_repo_plan_get`: adds `COALESCE(NULLIF(p.ledger_id, 0), ta.ledger_id) AS ledger_id` + `MF_RBAC_MASK_BALANCE_COL("ta") AS mask_bal_ta`, filters with `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("p", "ta")` (if `ledger_id > 0`) + `MF_RBAC_ASSET_VISIBLE_SQL("ta") AND MF_RBAC_ASSET_VISIBLE_SQL("fa") AND p.id = ?`, and wraps the returned array with `format_dca_plan_rows(arr)`.
   - In `mf_dca_repo_plan_create`: `INSERT INTO dca_plans (user_id, ledger_id, target_asset_id, ...)` writing `COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), (SELECT ledger_id FROM assets WHERE id = ?))`.
   - In `mf_dca_repo_plan_update` / `mf_dca_repo_plan_set_status` / `mf_dca_repo_plan_delete`: when `ledger_id > 0`, verify `id = ? AND COALESCE(NULLIF(dca_plans.ledger_id, 0), (SELECT ledger_id FROM assets WHERE id = dca_plans.target_asset_id)) = ? AND MF_LEDGER_SHARED_WRITE_FALLBACK_SQL("dca_plans", "target_asset_id")`; when `ledger_id <= 0`, keep `user_id = ? AND id = ?`.
   - In `mf_dca_repo_execution_list_by_plan`, `mf_dca_repo_execution_list_pending`, `mf_dca_repo_execution_get`: join `dca_plans p`, `assets ta`, `assets fa`, apply `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("p", "ta")` (when `ledger_id > 0`, else `e.user_id = ?`) + `MF_RBAC_ASSET_VISIBLE_SQL("ta") AND MF_RBAC_ASSET_VISIBLE_SQL("fa")`.
   - In `mf_dca_repo_execution_update_status`: when `ledger_id > 0`, verify `e.id = ? AND EXISTS (SELECT 1 FROM dca_plans p JOIN assets ta ON ta.id = p.target_asset_id WHERE p.id = e.plan_id AND MF_LEDGER_SHARED_WRITE_FALLBACK_SQL("p", "target_asset_id") AND COALESCE(NULLIF(p.ledger_id, 0), ta.ledger_id) = ?)`.

5. **`backend/src/application/dca/usecases.c`**:
   - Update `dca_validate_asset_perm(pool, user_id, req_ledger_id, asset_id, out_res, &out_asset_ledger_id)` to check `alid > 0 && req_ledger_id > 0 && alid != req_ledger_id` -> `out_res->code = 1004; "Forbidden: 不支持跨账本操作"`.
   - In `create_plan` & `update_plan`: validate both `target_asset_id` and `funding_asset_id`, reject `t_lid > 0 && f_lid > 0 && t_lid != f_lid` (`1004`), and set `plan.ledger_id = (cmd->ledger_id > 0) ? cmd->ledger_id : (t_lid > 0 ? t_lid : f_lid)`.
   - In `set_plan_status` & `delete_plan`: fetch plan first with `mf_dca_repo_plan_get(pool, user_id, ledger_id, id)`; if missing return `1003` (`Plan not found`); extract `target_asset_id` & `funding_asset_id` and run `dca_validate_asset_perm(..., MF_ACCOUNT_ACTION_WRITE_TX)` before mutating.
   - In `confirm_execution`: call `mf_dca_repo_execution_get(pool, cmd->user_id, cmd->ledger_id, cmd->exec_id)`, validate both assets with `dca_validate_asset_perm`, and set `ltx.ledger_id = (db_get_int(exec, "ledger_id") > 0) ? db_get_int(exec, "ledger_id") : cmd->ledger_id` (making sure `mf_dca_repo_execution_get` selects `COALESCE(NULLIF(p.ledger_id, 0), ta.ledger_id) AS ledger_id`).
   - In `skip_execution`: fetch `mf_dca_repo_execution_get(pool, user_id, ledger_id, exec_id)`; if missing return `1003`; validate `target_asset_id` & `funding_asset_id` with `dca_validate_asset_perm` (`1004` on viewer) before calling `mf_dca_repo_execution_update_status`.

6. **`backend/src/interfaces/http/controllers/dca_controller.c`**:
   - Add `int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer"); if (ledger_id <= 0) return;` in `dca_service_list_plans`, `dca_service_plan_summary`, `dca_service_get_plan`, `dca_service_list_executions`, `dca_service_list_pending_executions` and forward `ledger_id`.
   - Add `int64_t ledger_id = ctx_ledger_id(c, user_id, "editor"); if (ledger_id <= 0) return;` in `dca_service_confirm_execution` & `dca_service_skip_execution`, and forward `ledger_id` in all 6 write handlers (`create`, `update`, `set_status`, `delete`, `confirm`, `skip`).

- [ ] **Step 4: Build and run tests**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R "test_account_rbac|test_domain_repositories" --output-on-failure`
Expected: Both PASS.

- [ ] **Step 5: Commit Task 5**

```bash
git add backend/src/domain/dca/ \
        backend/src/application/dca/ \
        backend/src/infrastructure/repositories/dca_repo_impl.c \
        backend/src/interfaces/http/controllers/dca_controller.c \
        backend/tests/unit/test_account_rbac.c
git commit -m "fix(dca): 🐛 enforce ledger isolation, shared access, and account RBAC"
```

---

### Task 6: Align Cashflow Full Stack with Ledger Isolation, Shared Access & Account RBAC

**Files:**
- Modify: `backend/src/domain/cashflow/entity.h:12-27`
- Modify: `backend/src/domain/cashflow/repository.h:12-39`
- Modify: `backend/src/infrastructure/repositories/cashflow_repo_impl.h:1-8`
- Modify: `backend/src/infrastructure/repositories/cashflow_repo_impl.c:1-394`
- Modify: `backend/src/application/cashflow/commands.h:1-47`
- Modify: `backend/src/application/cashflow/usecases.h:1-42`
- Modify: `backend/src/application/cashflow/usecases.c:18-497`
- Modify: `backend/src/interfaces/http/controllers/cashflow_controller.c:10-250`
- Test: `backend/tests/unit/test_domain_repositories.c:1108-1133`, `backend/tests/unit/test_account_rbac.c`

- [ ] **Step 1: Write failing tests in `test_domain_repositories.c` & `test_account_rbac.c`**

1. Update `backend/tests/unit/test_domain_repositories.c:1120-1131` to pass `ledger_id` (`0` compatibility + test `schedule.ledger_id`):

```c
    int64_t schedule_id = 0;
    rc = mf_cashflow_repo_create(pool, &schedule, &schedule_id);
    assert(rc == 0 && schedule_id > 0);

    mf_cashflow_schedule_t* list = NULL;
    size_t count = 0;
    rc = mf_cashflow_repo_list_active(pool, user_id, 0, &list, &count);
    assert(rc == 0);
    assert(count > 0 && list != NULL);
    mf_cashflow_repo_free_list(list, count);
```

2. Include `"application/cashflow/usecases.h"` in `test_account_rbac.c`, add `test_cashflow_shared_ledger_and_rbac(pool)` and call it from `main()`:

```c
static void
test_cashflow_shared_ledger_and_rbac(csilk_db_pool_t* pool)
{
    printf("--- Testing Cashflow Ledger Isolation, Shared Access & Account RBAC ---\n");
    int64_t owner_id = 0, editor_id = 0;
    assert(mf_auth_repo_create(pool, "cf_owner", "h1", &owner_id) == 0);
    assert(mf_auth_repo_create(pool, "cf_editor", "h2", &editor_id) == 0);

    char s_oid[32], s_eid[32];
    snprintf(s_oid, sizeof(s_oid), "%lld", (long long)owner_id);
    snprintf(s_eid, sizeof(s_eid), "%lld", (long long)editor_id);

    csilk_json_t* lr1 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'CfL1', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid1 = db_get_int(csilk_json_array_get(lr1, 0), "id");
    csilk_json_free(lr1);
    csilk_json_t* lr2 = csilk_db_query_param_json(
        pool, "INSERT INTO ledgers (owner_id, name, currency) VALUES (?, 'CfL2', 'CNY') RETURNING id;",
        (const char*[]){s_oid, NULL});
    int64_t lid2 = db_get_int(csilk_json_array_get(lr2, 0), "id");
    csilk_json_free(lr2);

    char s_l1[32], s_l2[32];
    snprintf(s_l1, sizeof(s_l1), "%lld", (long long)lid1);
    snprintf(s_l2, sizeof(s_l2), "%lld", (long long)lid2);
    csilk_db_exec_param(
        pool, "INSERT INTO ledger_members (ledger_id, user_id, role) VALUES (?, ?, 'owner'), (?, ?, 'editor'), (?, ?, 'owner');",
        (const char*[]){s_l1, s_oid, s_l1, s_eid, s_l2, s_oid, NULL});

    csilk_db_exec_param(
        pool, "INSERT INTO categories (user_id, ledger_id, name, type, asset_type) VALUES (?, ?, 'CfCash1', 'asset', 'cash'), (?, ?, 'CfCash2', 'asset', 'cash');",
        (const char*[]){s_oid, s_l1, s_oid, s_l2, NULL});
    csilk_json_t* cr = csilk_db_query_param_json(
        pool, "SELECT id, name FROM categories WHERE user_id=?;", (const char*[]){s_oid, NULL});
    int64_t c1 = 0, c2 = 0;
    for (size_t i = 0; i < csilk_json_array_size(cr); i++) {
        csilk_json_t* r = csilk_json_array_get(cr, i);
        if (strcmp(csilk_json_get_string(r, "name"), "CfCash1") == 0) c1 = db_get_int(r, "id");
        if (strcmp(csilk_json_get_string(r, "name"), "CfCash2") == 0) c2 = db_get_int(r, "id");
    }
    csilk_json_free(cr);

    account_usecase_result_t acres = {0};
    create_account_cmd_t ac_op = {.user_id = owner_id, .ledger_id = lid1, .name = "CfOpen", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_mk = {.user_id = owner_id, .ledger_id = lid1, .name = "CfMask", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_no = {.user_id = owner_id, .ledger_id = lid1, .name = "CfNone", .institution = "bank_card", .currency = "CNY"};
    create_account_cmd_t ac_l2 = {.user_id = owner_id, .ledger_id = lid2, .name = "CfL2", .institution = "bank_card", .currency = "CNY"};
    assert(account_usecase_create(pool, &ac_op, &acres) == 0); int64_t acct_op = acres.id;
    assert(account_usecase_create(pool, &ac_mk, &acres) == 0); int64_t acct_mk = acres.id;
    assert(account_usecase_create(pool, &ac_no, &acres) == 0); int64_t acct_no = acres.id;
    assert(account_usecase_create(pool, &ac_l2, &acres) == 0); int64_t acct_l2 = acres.id;

    asset_usecase_result_t ares = {0};
    create_asset_cmd_t as_src = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_op, .name = "CfSrc", .current_value = 10000.0, .currency = "CNY"};
    create_asset_cmd_t as_op  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_op, .name = "CfTgtOpen", .current_value = 100.0, .currency = "CNY"};
    create_asset_cmd_t as_mk  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_mk, .name = "CfTgtMask", .current_value = 200.0, .currency = "CNY"};
    create_asset_cmd_t as_no  = {.user_id = owner_id, .ledger_id = lid1, .category_id = c1, .account_id = acct_no, .name = "CfTgtNone", .current_value = 300.0, .currency = "CNY"};
    create_asset_cmd_t as_l2  = {.user_id = owner_id, .ledger_id = lid2, .category_id = c2, .account_id = acct_l2, .name = "CfL2", .current_value = 400.0, .currency = "CNY"};
    assert(asset_usecase_create(pool, &as_src, &ares) == 0); int64_t src_id = ares.id;
    assert(asset_usecase_create(pool, &as_op, &ares) == 0);  int64_t op_id = ares.id;
    assert(asset_usecase_create(pool, &as_mk, &ares) == 0);  int64_t mk_id = ares.id;
    assert(asset_usecase_create(pool, &as_no, &ares) == 0);  int64_t no_id = ares.id;
    assert(asset_usecase_create(pool, &as_l2, &ares) == 0);  int64_t l2_id = ares.id;

    cashflow_usecase_result_t cfres = {0};
    create_cashflow_cmd_t bad_cross = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = l2_id, .name = "Bad", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 50.0};
    int64_t tmp_id = 0;
    assert(cashflow_usecase_create_schedule(pool, &bad_cross, &tmp_id, &cfres) != 0 && cfres.code == 1004);

    create_cashflow_cmd_t cf_op = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = op_id, .name = "OpenDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 50.0};
    create_cashflow_cmd_t cf_mk = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = mk_id, .name = "MaskDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 60.0};
    create_cashflow_cmd_t cf_no = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = no_id, .name = "NoneDiv", .flow_type = "dividend", .frequency = "monthly", .start_date = "2026-11-01", .expected_amount = 70.0};
    int64_t sid_op = 0, sid_mk = 0, sid_no = 0;
    assert(cashflow_usecase_create_schedule(pool, &cf_op, &sid_op, &cfres) == 0);
    assert(cashflow_usecase_create_schedule(pool, &cf_mk, &sid_mk, &cfres) == 0);
    assert(cashflow_usecase_create_schedule(pool, &cf_no, &sid_no, &cfres) == 0);

    // Owner confirms income on open & masked accounts in 2026-11
    int64_t tx_id = 0;
    confirm_cashflow_cmd_t inc_op = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = op_id, .amount = 50.0, .date = "2026-11-15", .name = "OpenInc"};
    confirm_cashflow_cmd_t inc_mk = {.user_id = owner_id, .ledger_id = lid1, .source_asset_id = src_id, .target_asset_id = mk_id, .amount = 60.0, .date = "2026-11-15", .name = "MaskInc"};
    assert(cashflow_usecase_confirm_income(pool, &inc_op, &tx_id, &cfres) == 0);
    assert(cashflow_usecase_confirm_income(pool, &inc_mk, &tx_id, &cfres) == 0);

    // Legacy NULL ledger_id on sid_op still appears under lid1
    char s_sop[32];
    snprintf(s_sop, sizeof(s_sop), "%lld", (long long)sid_op);
    csilk_db_exec_param(pool, "UPDATE cashflow_schedules SET ledger_id = NULL WHERE id = ?;", (const char*[]){s_sop, NULL});

    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_op, editor_id, "viewer", 0, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_mk, editor_id, "viewer", 1, 0, &acres) == 0);
    assert(account_permission_usecase_set(pool, owner_id, lid1, acct_no, editor_id, "none", 0, 0, &acres) == 0);

    csilk_json_t* list = NULL;
    assert(cashflow_usecase_list_schedules(pool, editor_id, lid1, &list, &cfres) == 0);
    assert(list != NULL && csilk_json_array_size(list) == 2); // NoneDiv excluded
    csilk_json_free(list);

    // Calendar actual_total excludes masked account (50.0, NOT 110.0)
    query_calendar_cmd_t cal_cmd = {.user_id = editor_id, .ledger_id = lid1, .year = 2026, .month = 11};
    csilk_json_t* cal = NULL;
    assert(cashflow_usecase_get_calendar(pool, &cal_cmd, &cal, &cfres) == 0 && cal != NULL);
    assert(db_get_num(cal, "actual_total") == 50.0);
    csilk_json_free(cal);

    // Editor with viewer permission on acct_op cannot delete sid_op (1004)
    assert(cashflow_usecase_delete_schedule(pool, editor_id, lid1, sid_op, &cfres) != 0 && cfres.code == 1004);
    printf("  [OK] Cashflow enforces ledger isolation, NULL fallback, shared access, calendar unmasked & delete RBAC\n");
}
```

- [ ] **Step 2: Run test to verify compilation fails on missing `ledger_id` signatures**

Run: `cmake --build backend/build --target test_account_rbac test_domain_repositories`
Expected: FAIL on too many arguments to `mf_cashflow_repo_list_active` and `cashflow_usecase_*`.

- [ ] **Step 3: Implement Cashflow Domain, Application, Repository, and Controller alignment**

1. **`backend/src/domain/cashflow/entity.h`**: add `int64_t ledger_id;` to `mf_cashflow_schedule_t`.
2. **`backend/src/domain/cashflow/repository.h`** & **`backend/src/infrastructure/repositories/cashflow_repo_impl.h`**:
   - Add `int64_t ledger_id` to `mf_cashflow_repo_list`, `mf_cashflow_repo_get`, `mf_cashflow_repo_delete`, `mf_cashflow_repo_list_active`, `mf_cashflow_repo_get_actual_events`, `cashflow_repo_list_detailed`, `cashflow_repo_get_detailed`.
3. **`backend/src/application/cashflow/commands.h`** & **`usecases.h`**:
   - Add `int64_t ledger_id;` to `create_cashflow_cmd_t`, `update_cashflow_cmd_t`, `confirm_cashflow_cmd_t`, `query_calendar_cmd_t`.
   - Add `int64_t ledger_id` to `cashflow_usecase_list_schedules`, `cashflow_usecase_get_schedule`, `cashflow_usecase_delete_schedule`.
4. **`backend/src/infrastructure/repositories/cashflow_repo_impl.c`**:
   - Include `"infrastructure/repositories/account_permission_repo_impl.h"`.
   - Update `SQL_SCHEDULE_CREATE` to insert `ledger_id`:

```c
static const char* SQL_SCHEDULE_CREATE =
    "INSERT INTO cashflow_schedules (user_id, ledger_id, source_asset_id, target_asset_id, name, "
    "                                flow_type, frequency, start_date, end_date, "
    "                                expected_amount, status, note) "
    "VALUES (?, COALESCE(NULLIF(CAST(NULLIF(?, '') AS BIGINT), 0), "
    "        (SELECT ledger_id FROM assets WHERE id = ?)), ?, ?, ?, ?, ?, ?, ?, ?, 'active', ?) RETURNING id";
```

   - Update `parse_schedule_row()` to set `out->ledger_id = (int64_t)db_get_int(row, "ledger_id");`.
   - Update `mf_cashflow_repo_list`, `cashflow_repo_list_detailed`, `mf_cashflow_repo_get`, `cashflow_repo_get_detailed`, `mf_cashflow_repo_list_active`: build dynamic `WHERE` (`ledger_id > 0` uses `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("s", "ta")`, `<= 0` uses `s.user_id = ?`) plus `AND MF_RBAC_ASSET_VISIBLE_SQL("sa") AND MF_RBAC_ASSET_VISIBLE_SQL("ta")`; include `COALESCE(NULLIF(s.ledger_id, 0), ta.ledger_id) AS ledger_id` in `SELECT`.
   - Update `mf_cashflow_repo_get_actual_events`: when `ledger_id > 0` use `MF_LEDGER_SHARED_ACCESS_FALLBACK_SQL("t", "a")` (else `t.user_id = ?`) plus `AND MF_RBAC_ASSET_UNMASKED_SQL("a")`.
   - Update `mf_cashflow_repo_update` & `mf_cashflow_repo_delete`: when `ledger_id > 0` enforce `COALESCE(NULLIF(cashflow_schedules.ledger_id, 0), (SELECT ledger_id FROM assets WHERE id = cashflow_schedules.target_asset_id)) = ? AND MF_LEDGER_SHARED_WRITE_FALLBACK_SQL("cashflow_schedules", "target_asset_id")`; when `<= 0` keep `user_id = ? AND id = ?`.
5. **`backend/src/application/cashflow/usecases.c`**:
   - Update `cashflow_validate_asset_perm(pool, user_id, req_ledger_id, asset_id, action, out_res, &out_alid)` to reject cross-ledger asset (`alid > 0 && req_ledger_id > 0 && alid != req_ledger_id`, code `1004`).
   - In `create_schedule` / `update_schedule` / `confirm_income`: validate `src_lid` and `tgt_lid` match when both `> 0`; assign `s.ledger_id = (cmd->ledger_id > 0) ? cmd->ledger_id : (tgt_lid > 0 ? tgt_lid : src_lid)`.
   - In `delete_schedule`: fetch schedule via `mf_cashflow_repo_get(pool, user_id, ledger_id, id, &existing)` (if `< 0`, return `1003`), validate `existing.source_asset_id` (`READ`) and `existing.target_asset_id` (`WRITE_TX`) before deleting.
6. **`backend/src/interfaces/http/controllers/cashflow_controller.c`**:
   - Add `int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer"); if (ledger_id <= 0) return;` in `api_cashflow_list_schedules`, `api_cashflow_get_schedule`, `api_cashflow_get_calendar`.
   - Pass `.ledger_id = ledger_id` in `api_cashflow_create_schedule`, `api_cashflow_update_schedule`, `api_cashflow_confirm`, and `ledger_id` argument in `api_cashflow_delete_schedule`.

- [ ] **Step 4: Build and run tests**

Run: `cmake --build backend/build --parallel 4 && ctest --test-dir backend/build -R "test_account_rbac|test_domain_repositories" --output-on-failure`
Expected: Both PASS.

- [ ] **Step 5: Commit Task 6**

```bash
git add backend/src/domain/cashflow/ \
        backend/src/application/cashflow/ \
        backend/src/infrastructure/repositories/cashflow_repo_impl.h \
        backend/src/infrastructure/repositories/cashflow_repo_impl.c \
        backend/src/interfaces/http/controllers/cashflow_controller.c \
        backend/tests/unit/test_account_rbac.c \
        backend/tests/unit/test_domain_repositories.c
git commit -m "fix(cashflow): 🐛 enforce ledger isolation, shared access, and account RBAC"
```

---

### Task 7: Harden Legacy IM Gateway Token Persistence & Run Full Verification Matrix

**Files:**
- Modify: `gateway/ilink/state.js:25-48`
- Modify: `gateway/index.js:20-92`
- Test: `gateway/adapters/test/cache.test.js:1-41`

- [ ] **Step 1: Write failing tests in `gateway/adapters/test/cache.test.js`**

Append two `test(...)` blocks to `gateway/adapters/test/cache.test.js`:

```javascript
test('IM bindings cache writes atomically with 0600 mode without altering parent dir mode', async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'im-cache-perm-'));
  fs.chmodSync(tmpDir, 0o755);
  const testCacheFile = path.join(tmpDir, 'bindings.json');
  process.env.MINEFOLIO_IM_CACHE_FILE = testCacheFile;

  delete require.cache[require.resolve('../../index.js')];
  const gw = require('../../index.js');

  gw.TOKEN_CACHE.clear();
  await Promise.all(
    Array.from({ length: 10 }, (_, i) =>
      Promise.resolve().then(() => gw.setCachedToken(`qq:u${i}`, `secret_${i}`))
    )
  );

  const fileMode = fs.statSync(testCacheFile).mode & 0o777;
  const dirMode = fs.statSync(tmpDir).mode & 0o777;
  assert.strictEqual(fileMode, 0o600);
  assert.strictEqual(dirMode, 0o755);

  const leftovers = fs.readdirSync(tmpDir).filter((f) => f.includes('.tmp.'));
  assert.deepStrictEqual(leftovers, []);

  // Existing loose 0644 cache file gets tightened on loadCache()
  fs.chmodSync(testCacheFile, 0o644);
  gw.TOKEN_CACHE.clear();
  gw.loadCache();
  assert.strictEqual(fs.statSync(testCacheFile).mode & 0o777, 0o600);

  fs.rmSync(tmpDir, { recursive: true, force: true });
});
```

- [ ] **Step 2: Run gateway tests to verify failure**

Run: `npm --prefix gateway test`
Expected: FAIL on `assert.strictEqual(fileMode, 0o600)` (currently `0o644` under default umask).

- [ ] **Step 3: Implement shared `writeJsonAtomic` hardening in `gateway/ilink/state.js` & `gateway/index.js`**

1. In `gateway/ilink/state.js:25-48`:

```javascript
function writeJsonAtomic(filePath, data, opts = {}) {
  const { tightenDir = true } = opts;
  const dir = path.dirname(filePath);
  if (tightenDir) {
    fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
    try {
      fs.chmodSync(dir, 0o700);
    } catch {
      // Ignore on platforms where chmod is restricted
    }
  } else if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  const tmp = `${filePath}.tmp.${process.pid}.${crypto.randomBytes(4).toString('hex')}`;
  const content = `${JSON.stringify(data, null, 2)}\n`;
  const fd = fs.openSync(tmp, 'w', 0o600);
  try {
    fs.writeFileSync(fd, content, 'utf8');
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
  fs.renameSync(tmp, filePath);
  try {
    fs.chmodSync(filePath, 0o600);
  } catch {
    // Ignore
  }
}
```

2. In `gateway/index.js`:
   - Import `const { writeJsonAtomic } = require('./ilink/state');` at line 22.
   - Replace `saveCache()` (lines 49-69):

```javascript
function saveCache() {
  try {
    const obj = {};
    for (const [k, v] of TOKEN_CACHE.entries()) {
      if (v && typeof v === 'object' && typeof v.token === 'string') {
        obj[k] = v;
      } else if (typeof v === 'string') {
        obj[k] = { token: v, expiresAt: Date.now() + getTokenCacheTtlMs() };
      }
    }
    writeJsonAtomic(CACHE_FILE, obj, { tightenDir: false });
  } catch (err) {
    console.warn('[gateway] saveCache failed:', err && err.message);
  }
}
```

   - In `loadCache()` (inside `if (fs.existsSync(CACHE_FILE))` before/after reading):

```javascript
      try {
        fs.chmodSync(CACHE_FILE, 0o600);
      } catch {
        // Ignore on platforms where chmod is restricted
      }
```

- [ ] **Step 4: Commit Task 7**

Run `npm --prefix gateway test` (Expected: PASS) and commit:

```bash
git add gateway/ilink/state.js gateway/index.js gateway/adapters/test/cache.test.js
git commit -m "fix(gateway): 🐛 harden IM token cache persistence with 0600 atomic writes"
```

- [ ] **Step 5: Run Full Verification Matrix (CTest + ASan + Integration Suites + Gateway)**

Run sequentially in resource-bounded environment:

```bash
npm --prefix gateway test
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh unit backend/build
./backend/tests/run_isolated.sh build backend/build-asan
./backend/tests/run_isolated.sh unit backend/build-asan
bash backend/tests/test_link.sh
bash backend/tests/test_dca_cashflow.sh
```
*(If PostgreSQL is running locally with `MINEFOLIO_DB_DRIVER=postgres` env configured, also run `bash backend/tests/test_dca_cashflow.sh && bash backend/tests/test_link.sh` against PostgreSQL.)*

Expected:
- `npm --prefix gateway test`: all tests PASS.
- All 84+ unit tests in `backend/build` and `backend/build-asan` PASS with 0 ASan/UBSan findings.
- `test_link.sh` and `test_dca_cashflow.sh` PASS with 0 failures.
