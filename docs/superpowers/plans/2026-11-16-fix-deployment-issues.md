# Minefolio Deployment Issues Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the 7 issues identified in the deployed Minefolio service (Nginx 403 on `/assets`, duplicate exchange rate keys, transaction `ledger_id` NULL, news report title persistence, skill lazy seeding, transaction date defaulting, and Element Plus radio deprecation warnings) with isolated environment testing and strict memory limits.

**Architecture:**
- Frontend Nginx config fix for SPA try_files to prevent physical directory clash with `/assets` route.
- Backend C repository deduplication in `market_repo_impl.c` for exchange rates JSON generation.
- Flyway SQL migration `V025` for backfilling `transactions.ledger_id` in PostgreSQL and SQLite.
- Backend C fix in `ai_news_service.c` to update report title upon completion.
- Backend C lazy seed in `ai_skill_controller.c` when user skills are empty.
- Frontend Vue 3 fixes in `Transactions.vue` and radio components for UX and framework deprecation warnings.

**Tech Stack:** C23 (csilk framework), PostgreSQL 16, SQLite3, Vue 3, TypeScript, Vite, Element Plus, Playwright.

**Spec:** `/home/quintin/.gemini/antigravity-cli/brain/be9af027-0fd2-410e-9dec-86528f476c4a/minefolio_service_test_report.md`

## Global Constraints
- **Memory Safety Limits:** All builds and test executions must have strict memory caps:
  - C Compilation: `cmake --build build -j2` (maximum 2 parallel compiler jobs).
  - Node.js processes: `NODE_OPTIONS="--max-old-space-size=1024"` (1GB heap cap).
  - Playwright / Headless Browser: `--js-flags="--max-old-space-size=512"` and `--disable-dev-shm-usage`.
- **Isolation:** All modifications must be made within the git worktree (`.worktrees/fix-deployment-issues`).
- **DDD Compliance:** Maintain C backend DDD 4-layer dependency rules. No SQL in controllers/usecases; all SQL in `infrastructure/repositories/`.

## Review Focus
1. Nginx static asset fetching: real static files under `/assets/*.js` and `/assets/*.css` must continue to return HTTP 200 with correct MIME types while route `/assets` falls back to `/index.html`.
2. Exchange rates deduplication: all 7 major currencies (CNY, USD, EUR, HKD, JPY, GBP, USDT) must be present exactly once with positive numbers.
3. Transaction ledger filtering: backfilling `ledger_id` must match `asset.ledger_id` and must not corrupt existing non-null `ledger_id` values.
4. AI news report title: completed report must have a clean descriptive title instead of "正在生成今日市场脉搏…".
5. Compilation memory footprint: build memory must stay below 2GB throughout the build process.

---

### Task 1: Fix Nginx SPA Routing Conflict for `/assets`

**Files:**
- Modify: `nginx/minefolio.docker.conf:9-14`
- Modify: `nginx/minefolio.conf:9-14`

**Interfaces:**
- Nginx `location /`: serves `/index.html` for client-side SPA routes while preserving direct static file access.

- [ ] **Step 1: Check existing Nginx configuration**

Examine `nginx/minefolio.docker.conf` lines 9-14:
```nginx
location / {
    root /opt/minefolio/frontend/dist;
    try_files $uri $uri/ /index.html;
}
```

- [ ] **Step 2: Update `try_files` in both Nginx config files**

Change `try_files $uri $uri/ /index.html;` to `try_files $uri /index.html;`.
When `$uri` does not match an exact static file (e.g. `/assets` directory or `/dashboard`), Nginx falls back directly to `/index.html`.
When `$uri` matches an actual file (e.g. `/assets/index-B9MvIddC.js`, `/favicon.ico`), Nginx serves the file directly.

- [ ] **Step 3: Verify Nginx configuration syntax**

Test the configuration inside the worktree / container:
`docker exec minefolio-nginx nginx -t` (or validate syntax).

- [ ] **Step 4: Commit**

```bash
git add nginx/minefolio.docker.conf nginx/minefolio.conf
git commit -m "fix(nginx): remove directory lookup from try_files to prevent 403 on /assets"
```

---

### Task 2: Deduplicate Exchange Rates in `market_repo_impl.c`

**Files:**
- Modify: `backend/src/infrastructure/repositories/market_repo_impl.c:282-317`
- Test: `backend/tests/unit/test_market_repo_rates.c` (or add assertion to existing test)

**Interfaces:**
- Produces: `csilk_json_t* mf_market_repo_get_all_exchange_rates(void* pool)` returning a JSON object where each currency code key is unique.

- [ ] **Step 1: Write unit test verifying exchange rates deduplication**

Ensure `mf_market_repo_get_all_exchange_rates` outputs each currency key at most once.

- [ ] **Step 2: Implement deduplication in `mf_market_repo_get_all_exchange_rates`**

In `backend/src/infrastructure/repositories/market_repo_impl.c`:
Track already added currency codes (e.g. using a fixed-size string array or checking `csilk_json_get_string(out, cur)` / `csilk_json_get_child(out, cur)`) before calling `csilk_json_add_number(out, cur, rate)`.

- [ ] **Step 3: Compile and run unit test with memory limit**

Run: `cmake --build backend/build -j2 && ctest --test-dir backend/build -j2`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add backend/src/infrastructure/repositories/market_repo_impl.c
git commit -m "fix(market): deduplicate currency keys in exchange rates repository"
```

---

### Task 3: Database Migration V025 Backfill `transactions.ledger_id`

**Files:**
- Create: `backend/sql/migrations/postgres/V025__backfill_tx_ledger_id.sql`
- Create: `backend/sql/migrations/sqlite/V025__backfill_tx_ledger_id.sql`

**Interfaces:**
- Backfills `transactions.ledger_id` from parent `assets.ledger_id` (fallback to owner's default ledger).

- [ ] **Step 1: Write migration SQL for PostgreSQL**

```sql
-- V025: Backfill transactions.ledger_id from associated assets or default ledger
UPDATE transactions t
SET ledger_id = COALESCE(
    (SELECT a.ledger_id FROM assets a WHERE a.id = t.asset_id),
    (SELECT l.id FROM ledgers l WHERE l.owner_id = t.user_id AND l.is_default = 1)
)
WHERE t.ledger_id IS NULL;
```

- [ ] **Step 2: Write migration SQL for SQLite**

Mirror the exact query for SQLite compatibility.

- [ ] **Step 3: Verify migration file format and checksum**

Verify with `test_migration` unit test.

- [ ] **Step 4: Commit**

```bash
git add backend/sql/migrations/postgres/V025__backfill_tx_ledger_id.sql backend/sql/migrations/sqlite/V025__backfill_tx_ledger_id.sql
git commit -m "fix(db): add V025 migration to backfill transactions.ledger_id"
```

---

### Task 4: Fix AI News Report Completed Title in `ai_news_service.c`

**Files:**
- Modify: `backend/src/services/ai/ai_news_service.c:158-165`

**Interfaces:**
- When AI news execution finishes successfully (`status='done'`), updates `title` to a descriptive string (e.g. `每日市场脉搏 — YYYY-MM-DD`).

- [ ] **Step 1: Inspect `ai_news_service.c` update statement**

Current:
```c
csilk_db_query_param_json(
    pool,
    "UPDATE ai_news_reports SET analysis=?, status='done' WHERE id=? AND user_id=?",
    (const char*[]){res.final_content, id_str, uid_str, NULL});
```

- [ ] **Step 2: Update SQL to set appropriate completed title**

```c
char title_buf[128];
snprintf(title_buf, sizeof(title_buf), "每日市场脉搏 — %s", date_str);
csilk_db_query_param_json(
    pool,
    "UPDATE ai_news_reports SET title=?, analysis=?, status='done' WHERE id=? AND user_id=?",
    (const char*[]){title_buf, res.final_content, id_str, uid_str, NULL});
```

- [ ] **Step 3: Compile with memory limit**

Run: `cmake --build backend/build -j2`
Expected: 0 warnings, 0 errors.

- [ ] **Step 4: Commit**

```bash
git add backend/src/services/ai/ai_news_service.c
git commit -m "fix(news): update report title upon completion in ai_news_service"
```

---

### Task 5: Lazy Seed Defaults for AI Skills

**Files:**
- Modify: `backend/src/interfaces/http/controllers/ai_skill_controller.c`

**Interfaces:**
- In `api_ai_skill_list`: if `skill_usecase_list` returns 0 items for the user, call `skill_usecase_seed_defaults(pool, user_id)` and reload.

- [ ] **Step 1: Check `api_ai_skill_list` implementation**

Review current `api_ai_skill_list` in `ai_skill_controller.c`.

- [ ] **Step 2: Add lazy seed logic**

If returned skills array size is 0 and no filters were provided, call `skill_usecase_seed_defaults(pool, user_id)` and query again.

- [ ] **Step 3: Compile and run skills tests**

Run: `cmake --build backend/build -j2 && ctest --test-dir backend/build -R skill -j2`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add backend/src/interfaces/http/controllers/ai_skill_controller.c
git commit -m "fix(skills): lazy seed default skills on empty list query"
```

---

### Task 6: Default Transaction Date to Today in `Transactions.vue`

**Files:**
- Modify: `frontend/src/views/Transactions.vue:634-648`

**Interfaces:**
- `openDialog()` initializes `form.transaction_date` with today's date formatted as `YYYY-MM-DD`.

- [ ] **Step 1: Inspect `openDialog` in `Transactions.vue`**

Notice line 646: `transaction_date: txn ? txn.transaction_date : ''`.

- [ ] **Step 2: Change to default to today's date**

```typescript
const todayStr = new Date().toISOString().slice(0, 10)
transaction_date: txn ? txn.transaction_date : todayStr
```

- [ ] **Step 3: Verify frontend type check with memory limit**

Run: `NODE_OPTIONS="--max-old-space-size=1024" npm --prefix frontend run build`
Expected: vue-tsc passes with 0 errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "fix(transactions): default transaction date to today in creation dialog"
```

---

### Task 7: Clean Up Element Plus `el-radio` Deprecation Warnings

**Files:**
- Modify: `frontend/src/views/Settings.vue` (and any other view using `el-radio :label="..."`)

**Interfaces:**
- In Element Plus 2.8+, `label` used as `value` is deprecated; replace with `value="..."`.

- [ ] **Step 1: Search for `<el-radio ` and `<el-radio-button ` across `frontend/src/`**

Find all usages where `label` acts as value.

- [ ] **Step 2: Replace with `value` or appropriate props**

Update attributes to `value`.

- [ ] **Step 3: Build frontend**

Run: `NODE_OPTIONS="--max-old-space-size=1024" npm --prefix frontend run build`
Expected: 0 errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/
git commit -m "refactor(frontend): replace deprecated el-radio label with value prop"
```

---

### Task 8: End-to-End Verification with Memory Limits in Isolated Environment

**Files:**
- Test script: `scratch/verify_all_fixes.js`

**Interfaces:**
- E2E Playwright verification script running under memory caps:
  - Node memory: `NODE_OPTIONS="--max-old-space-size=512"`
  - Chromium memory: `--js-flags="--max-old-space-size=512"`

- [ ] **Step 1: Run complete backend unit tests**

Command: `cmake --build backend/build -j2 && ctest --test-dir backend/build --output-on-failure -j2`
Expected: All test suites pass.

- [ ] **Step 2: Run frontend build and typecheck**

Command: `NODE_OPTIONS="--max-old-space-size=1024" npm --prefix frontend run build`
Expected: Build succeeds with 0 errors.

- [ ] **Step 3: Verify Nginx /assets fix via curl and browser**

Command: `curl -I http://127.0.0.1/assets`
Expected: Returns HTTP 200 (or index.html fallback), NOT 403 Forbidden.

- [ ] **Step 4: Final verification report**

Generate summary of all verified fixes.
