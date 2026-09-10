# Expense Report Isolation Implementation Plan

> **For agentic workers:** Execute this plan task-by-task with tests before production changes.

**Goal:** Ensure every expense monthly/report query is scoped to the authenticated user and active ledger, with regression coverage for cross-user and cross-ledger isolation.

**Architecture:** The report request boundary obtains the active ledger through the existing membership-aware `ctx_ledger_id()` helper. The expense report service and daily-expense monthly usecase pass that ledger ID into repository queries; SQL includes explicit `user_id=? AND ledger_id=?` predicates and does not treat `NULL` ledger IDs as belonging to the active ledger.

**Tech Stack:** C23, Cilk HTTP API, SQLite/PostgreSQL-compatible parameterized SQL, shell integration tests, CTest.

---

### Task 1: Add failing cross-scope regression coverage

**Files:**
- Modify: `backend/tests/test_full.sh`
- Inspect: `backend/tests/test_ledgers.sh`, `backend/src/interfaces/http/controllers/report_controller.c`, `backend/src/common/ctx.h`

- [ ] Add an integration scenario that creates two users, two ledgers, and expense data with distinct amounts/categories/tags in each scope. Authenticate each user and select each ledger with `X-Ledger-Id`.
- [ ] Assert monthly totals, category, tag, daily breakdown, trend, yearly, category, and tag endpoints do not include records from another user or another ledger.
- [ ] Run the focused/full integration test and confirm it fails against the current implementation because the daily-expense monthly repository ignores `ledger_id` and the report service does not select the active ledger.

Expected failing evidence: a report for the active ledger includes a known amount/category/tag inserted into another ledger or user.

### Task 2: Thread active ledger through expense monthly APIs

**Files:**
- Modify: `backend/src/domain/daily_expense/repository.h`
- Modify: `backend/src/infrastructure/repositories/daily_expense_repo_impl.c`
- Modify: `backend/src/application/daily_expense/usecases.c`
- Modify: `backend/src/interfaces/http/controllers/daily_expense_controller.c`

- [ ] Change the four monthly repository signatures to accept `int64_t ledger_id`.
- [ ] Add `user_id=? AND ledger_id=? AND expense_date LIKE ?` predicates to totals, category, tag, and daily queries; for tag joins also scope `tags` through the expense/user/ledger relationship.
- [ ] Pass the validated active ledger from the controller through the usecase and repository with parameter arrays in the same order as predicates.
- [ ] Return an error when an active ledger cannot be resolved; do not silently query by user only.

### Task 3: Scope expense report service queries

**Files:**
- Modify: `backend/src/services/report_expense_service.c`
- Modify: `backend/src/services/report_expense_service.h` only if helper signatures require documentation changes

- [ ] Resolve `ledger_id = ctx_ledger_id(c, user_id, "viewer")` at the start of each expense report handler.
- [ ] Add `ledger_id` to monthly, trend, yearly, category, and tag SQL predicates and parameter arrays.
- [ ] Ensure category/tag joins cannot pull labels from another user/ledger; use matching `ledger_id` columns where available and `user_id` ownership checks.
- [ ] Preserve existing response shapes and endpoint behavior for authorized requests.

### Task 4: Audit adjacent report queries and legacy paths

**Files:**
- Inspect/Modify as needed: `backend/src/application/report/usecases.c`, `backend/src/services/report_asset_service.c`, `backend/src/services/report_holdings_service.c`, `backend/src/infrastructure/repositories/ai_repo_impl.c`, `backend/src/services/ai/workflows/monthly_review.c`, `backend/src/services/ai/workflows/cashflow_forecast.c`

- [ ] Search every expense report SQL and monthly AI expense query for missing scope predicates.
- [ ] For queries that cannot safely receive ledger ID because their schema/write path is not ledger-aware, make the query explicitly fail closed or thread ledger ID through the call chain rather than using `user_id` alone.
- [ ] Do not change unrelated report response formats.

### Task 5: Verify and review

**Files:**
- Modify: `findings.md` only if the implementation changes the previously documented findings
- Modify: `progress.md`

- [ ] Run `cmake --build backend/build --parallel`.
- [ ] Run `backend/build/ctest --output-on-failure`.
- [ ] Run `backend/tests/test_full.sh` and the ledger/report-focused scripts.
- [ ] Search for remaining expense report SQL without `user_id` and `ledger_id` predicates.
- [ ] Perform a final cross-layer review of controller → `ctx_ledger_id` → service/usecase → repository → SQL and record residual limitations, especially legacy `NULL ledger_id` rows.
