# Database Migration System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a formal, Flyway-style versioned database migration system for Minefolio, supporting discovery, SHA-256 validation, transactional execution, distributed locking, and zero-loss auto-baselining for pre-existing production databases, completely replacing 600+ lines of ad-hoc column detection in `common/db.c`.

**Architecture:** Build migration files under `backend/sql/migrations/{sqlite,postgres}/V<num>__<name>.sql`. Implement a native C migration engine under `backend/src/infrastructure/database/migration/` (`migration_engine.h/.c`, `checksum.h/.c`, `lock.h/.c`). Refactor `db_run_migrations()` in `common/db.c` to delegate entirely to the migration engine.

**Tech Stack:** C23, SQLite3, PostgreSQL, OpenSSL (SHA-256), CMake, CTest.

---

### Task 1: Create Versioned SQL Migration Files for SQLite and PostgreSQL

**Files:**
- Create: `backend/sql/migrations/sqlite/V001__initial_auth_and_system.sql`
- Create: `backend/sql/migrations/sqlite/V002__categories_and_assets.sql`
- Create: `backend/sql/migrations/sqlite/V003__transactions_and_expenses.sql`
- Create: `backend/sql/migrations/sqlite/V004__ledgers_and_members.sql`
- Create: `backend/sql/migrations/sqlite/V005__ai_traces_and_settings.sql`
- Create: `backend/sql/migrations/sqlite/V006__dca_and_cashflow.sql`
- Create: `backend/sql/migrations/sqlite/V007__market_quotes_and_fx.sql`
- Create: `backend/sql/migrations/postgres/V001__initial_auth_and_system.sql`
- Create: `backend/sql/migrations/postgres/V002__categories_and_assets.sql`
- Create: `backend/sql/migrations/postgres/V003__transactions_and_expenses.sql`
- Create: `backend/sql/migrations/postgres/V004__ledgers_and_members.sql`
- Create: `backend/sql/migrations/postgres/V005__ai_traces_and_settings.sql`
- Create: `backend/sql/migrations/postgres/V006__dca_and_cashflow.sql`
- Create: `backend/sql/migrations/postgres/V007__market_quotes_and_fx.sql`

- [ ] **Step 1: Write SQLite migration scripts V001 through V007**
Decompose the complete SQLite schema into 7 modular, cleanly versioned migration files covering auth, categories/assets, transactions/expenses, ledgers/rbac, AI traces/settings, DCA/cashflow, and market quotes/fx.

- [ ] **Step 2: Write PostgreSQL migration scripts V001 through V007**
Decompose the complete PostgreSQL schema into corresponding modular migration files with native PG types (`BIGSERIAL`, `DOUBLE PRECISION`, `TIMESTAMP`).

- [ ] **Step 3: Update CMake to copy `sql/migrations/` on build**
Modify `backend/CMakeLists.txt` to copy the `sql/migrations` directory to `${CMAKE_BINARY_DIR}/sql/migrations` post-build.

- [ ] **Step 4: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS (all migration files copied to build output).

- [ ] **Step 5: Commit**
Run: `git add backend/sql/migrations/ backend/CMakeLists.txt && git commit -m "feat(migration): ✨ create versioned migration files for SQLite and PostgreSQL"`

---

### Task 2: Checksum & Distributed Migration Lock Modules

**Files:**
- Create: `backend/src/infrastructure/database/migration/checksum.h`
- Create: `backend/src/infrastructure/database/migration/checksum.c`
- Create: `backend/src/infrastructure/database/migration/lock.h`
- Create: `backend/src/infrastructure/database/migration/lock.c`

- [ ] **Step 1: Implement `checksum.h` & `checksum.c`**
Implement SHA-256 calculation for file contents with line-ending normalization (`\r\n` -> `\n`) and trailing whitespace trimming, returning a 64-character lowercase hex string.

- [ ] **Step 2: Implement `lock.h` & `lock.c`**
Implement migration locking:
- Create `schema_migration_lock` table if not exists.
- `mf_migration_lock_acquire`: Acquire lock atomically (with timeout / busy retries in SQLite, advisory lock in PostgreSQL).
- `mf_migration_lock_release`: Release lock atomically.

- [ ] **Step 3: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 4: Commit**
Run: `git add backend/src/infrastructure/database/migration/checksum.* backend/src/infrastructure/database/migration/lock.* && git commit -m "feat(migration): ✨ implement SHA-256 normalized checksum and migration locking"`

---

### Task 3: Migration Engine Core & Auto-Baseline Logic

**Files:**
- Create: `backend/src/infrastructure/database/migration/migration_engine.h`
- Create: `backend/src/infrastructure/database/migration/migration_engine.c`

- [ ] **Step 1: Define migration engine interface in `migration_engine.h`**
Declare `mf_migration_item_t`, `mf_migration_engine_t`, `mf_migration_engine_new`, `mf_migration_engine_free`, `mf_migration_discover`, `mf_migration_validate`, `mf_migration_apply`, and `mf_migration_status`.

- [ ] **Step 2: Implement directory discovery & sorting**
Implement `mf_migration_discover` scanning `sql/migrations/<dialect>/`, parsing `V<num>__<name>.sql`, and sorting ascending by version number.

- [ ] **Step 3: Implement Auto-Baseline detection for existing databases**
If `schema_migrations` table does not exist, check if `users` table already exists in the database.
If yes: create `schema_migrations` table and insert records for baseline migrations (V001..V007) with `execution_time_ms = 0`, marking them as already applied. Zero destructive DDL executed on existing tables!
If no: create `schema_migrations` table ready for fresh apply.

- [ ] **Step 4: Implement `validate` & `apply`**
- `validate`: Compare checksum of all applied migrations against file on disk. If mismatch, return error.
- `apply`: For each unapplied migration, run in transaction (`mf_tx_begin` -> `mf_tx_commit`), measure elapsed time in milliseconds, and insert into `schema_migrations`.

- [ ] **Step 5: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 6: Commit**
Run: `git add backend/src/infrastructure/database/migration/migration_engine.* && git commit -m "feat(migration): ✨ implement migration engine with discovery, validation, apply, and auto-baseline"`

---

### Task 4: Standalone Migration Engine Unit Test Suite

**Files:**
- Create: `backend/tests/unit/test_migration_engine.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write test cases in `test_migration_engine.c`**
Verify:
1. File discovery & ascending version sorting.
2. SHA-256 checksum consistency across CRLF differences.
3. Fresh apply: applies V001..V007 to empty database, creates tables, populates `schema_migrations`.
4. Tampering detection: modifying an applied migration causes `validate` to fail.
5. Auto-Baseline: pre-existing database with `users` table is automatically baselined without DDL errors and without data loss.
6. Concurrent lock: second lock acquire fails while lock is held.

- [ ] **Step 2: Register test in `CMakeLists.txt`**
Add `test_migration_engine` executable and register with `add_test`.

- [ ] **Step 3: Run CTest to verify all pass**
Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_migration_engine --output-on-failure`
Expected: PASS (100%).

- [ ] **Step 4: Commit**
Run: `git add backend/tests/unit/test_migration_engine.c backend/CMakeLists.txt && git commit -m "test(migration): ✅ add comprehensive migration engine unit test suite"`

---

### Task 5: Refactor `common/db.c` to Delegate to Migration Engine

**Files:**
- Modify: `backend/src/common/db.c`

- [ ] **Step 1: Replace ad-hoc checks in `db_run_migrations()`**
Remove the 600+ lines of manual `col_exists`, `ALTER TABLE`, and table rewrites.
Replace with clean call to `mf_migration_engine_new()` and `mf_migration_apply()`.

- [ ] **Step 2: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 3: Verify with full integration test `test_link.sh`**
Run: `./backend/tests/test_link.sh`
Expected: PASS (139/139 assertions pass).

- [ ] **Step 4: Commit**
Run: `git add backend/src/common/db.c && git commit -m "refactor(database): ♻️ replace ad-hoc column detection with formal migration engine"`

---

### Task 6: Full Dual-Engine Verification & Regression Testing

**Files:**
- Unit tests: `test_currency` ... `test_database_repository`, `test_migration_engine`
- Integration tests: `test_link.sh`, `test_ledgers.sh`, `test_2fa.sh`, `test_dca_cashflow.sh`, `test_ai_trace.sh`, `test_market_sync.sh`, `test_fx_oauth.sh`

- [x] **Step 1: Run all 26 CTest unit test suites**
Run: `ctest --test-dir backend/build --output-on-failure`
Expected: 26/26 PASS.

- [x] **Step 2: Run all 7 bash integration test suites**
Run all 7 test scripts.
Expected: ALL PASS.

- [x] **Step 3: Run frontend build and tests**
Run: `npm --prefix frontend test && npm --prefix frontend run build`
Expected: PASS (0 errors).

- [x] **Step 4: Final workspace check**
Run: `git status`
Expected: Clean working tree.
