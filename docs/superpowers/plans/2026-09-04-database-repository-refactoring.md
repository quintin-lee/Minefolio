# Database / Repository Layer Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor Minefolio database access and repository layers, providing clean infrastructure abstractions (database, transaction with savepoint, statement with typed bindings, sqlite/postgres adapters) and pure data-mapping repositories strictly free from business rules/computations/HTTP/auth.

**Architecture:** Build a C23 unified database layer under `infrastructure/database/` with `database.h`, `transaction.h`, `statement.h`, backed by concrete `sqlite/` and `postgres/` adapters. Build domain data-mapper repositories under `repositories/` with zero database engine branch checks, and provide dual-engine consistency testing across SQLite and PostgreSQL.

**Tech Stack:** C23, CMake, SQLite3, PostgreSQL (libpq/csilk adapter), CTest.

---

### Task 1: Core Database API & Infrastructure Headers

**Files:**
- Create: `backend/src/infrastructure/database/database.h`
- Create: `backend/src/infrastructure/database/transaction.h`
- Create: `backend/src/infrastructure/database/statement.h`

- [ ] **Step 1: Write header definitions for `database.h`**
Define database engine enum (`MF_DB_ENGINE_SQLITE`, `MF_DB_ENGINE_POSTGRES`), connection configuration struct (`mf_db_config_t`), lifecycle handles (`mf_db_open`, `mf_db_close`), engine query (`mf_db_get_engine`), execute and execute_with_retry APIs.

- [ ] **Step 2: Write header definitions for `transaction.h`**
Define `mf_tx_t`, transaction lifecycle (`mf_tx_begin`, `mf_tx_commit`, `mf_tx_rollback`), savepoint operations (`mf_tx_savepoint`, `mf_tx_rollback_to_savepoint`, `mf_tx_release_savepoint`), and query/execute within transactions (`mf_tx_execute`, `mf_tx_prepare`).

- [ ] **Step 3: Write header definitions for `statement.h`**
Define `mf_stmt_t` and `mf_result_t`. Define typed parameter binding (`bind_int64`, `bind_double`, `bind_text`, `bind_bool`, `bind_null`), execution (`mf_stmt_execute`, `mf_stmt_query`), and result cursor navigation (`mf_result_next`, `mf_result_get_int64`, `mf_result_get_double`, `mf_result_get_text`, `mf_result_get_bool`, `mf_result_is_null`, `mf_result_free`, `mf_result_to_json`).

- [ ] **Step 4: Verify headers compile cleanly**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS (new headers parsed without syntax errors).

- [ ] **Step 5: Commit**
Run: `git add backend/src/infrastructure/database/database.h backend/src/infrastructure/database/transaction.h backend/src/infrastructure/database/statement.h && git commit -m "feat(database): ✨ define unified database, transaction, and statement headers"`

---

### Task 2: SQLite Adapter Implementation

**Files:**
- Create: `backend/src/infrastructure/database/sqlite/sqlite_adapter.h`
- Create: `backend/src/infrastructure/database/sqlite/sqlite_adapter.c`

- [ ] **Step 1: Write SQLite adapter interface**
Declare SQLite adapter vtable operations for open, close, begin, commit, rollback, savepoint (`SAVEPOINT %s`), rollback to savepoint (`ROLLBACK TO %s`), release savepoint (`RELEASE %s`), prepare statement, bind typed parameters, step/execute, and result mapping.

- [ ] **Step 2: Implement SQLite adapter logic**
Implement connection initialization with `PRAGMA journal_mode=WAL;`, `PRAGMA busy_timeout=5000;`, and `PRAGMA synchronous=NORMAL;`. Implement statement execution using SQLite C APIs / csilk db bridge. Implement `mf_result_t` mapping from SQLite columns with type safety.

- [ ] **Step 3: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 4: Commit**
Run: `git add backend/src/infrastructure/database/sqlite/ && git commit -m "feat(database): ✨ implement SQLite adapter with WAL and savepoints"`

---

### Task 3: PostgreSQL Adapter Implementation & Dialect Helpers

**Files:**
- Create: `backend/src/infrastructure/database/postgres/postgres_adapter.h`
- Create: `backend/src/infrastructure/database/postgres/postgres_adapter.c`

- [ ] **Step 1: Write PostgreSQL adapter interface**
Declare PostgreSQL adapter vtable operations including placeholder translator (converts standard `?` placeholders outside quotes to `$1, $2, ...`), savepoint syntax (`SAVEPOINT %s`, `ROLLBACK TO SAVEPOINT %s`, `RELEASE SAVEPOINT %s`), and connection retry policy with exponential backoff.

- [ ] **Step 2: Implement PostgreSQL adapter logic**
Implement SQL placeholder parser translating `?` to `$1..$n`, savepoint generation, and statement binding. Provide fallback emulation/bridge against csilk postgres driver or contract verification when libpq is active or mock-tested.

- [ ] **Step 3: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 4: Commit**
Run: `git add backend/src/infrastructure/database/postgres/ && git commit -m "feat(database): ✨ implement PostgreSQL adapter and dialect parameter translation"`

---

### Task 4: Unified Database Engine Dispatcher & Statement/Transaction Lifecycle

**Files:**
- Create: `backend/src/infrastructure/database/database.c`
- Create: `backend/src/infrastructure/database/transaction.c`
- Create: `backend/src/infrastructure/database/statement.c`

- [ ] **Step 1: Implement `database.c`**
Implement `mf_db_open`, `mf_db_close`, `mf_db_get_engine`, `mf_db_execute`, and `mf_db_execute_with_retry`. Dispatch to SQLite or PostgreSQL adapter based on configuration.

- [ ] **Step 2: Implement `transaction.c`**
Implement `mf_tx_begin`, `mf_tx_commit`, `mf_tx_rollback`, `mf_tx_savepoint`, `mf_tx_rollback_to_savepoint`, and `mf_tx_release_savepoint`. Maintain active connection ownership per transaction handle.

- [ ] **Step 3: Implement `statement.c`**
Implement `mf_stmt_prepare`, `mf_stmt_bind_*`, `mf_stmt_execute`, `mf_stmt_query`, cursor iteration `mf_result_next`, typed column accessors, and `mf_result_to_json` bridge.

- [ ] **Step 4: Verify build**
Run: `cmake --build backend/build --target minefolio`
Expected: PASS.

- [ ] **Step 5: Commit**
Run: `git add backend/src/infrastructure/database/database.c backend/src/infrastructure/database/transaction.c backend/src/infrastructure/database/statement.c && git commit -m "feat(database): ✨ implement database dispatcher, transactions, and statements"`

---

### Task 5: Standalone Dual-Engine Unit Test Suite

**Files:**
- Create: `backend/tests/unit/test_database_repository.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Write test cases in `test_database_repository.c`**
Write unit test verifying:
1. SQLite in-memory open, execute, query, prepared statement typed bindings (`bind_int64`, `bind_double`, `bind_text`, `bind_null`).
2. SQLite transaction rollback (inserted data disappears).
3. SQLite savepoint rollback (`sp1` rollback retains row 1 and removes row 2).
4. PostgreSQL adapter placeholder translation (`SELECT ? + ?` -> `SELECT $1 + $2`), savepoint command syntax generation (`ROLLBACK TO SAVEPOINT`).
5. PostgreSQL connection & transaction execution (via mock adapter or real PG if configured).

- [ ] **Step 2: Register test in CMakeLists.txt**
Add `test_database_repository` executable and `add_test(NAME test_database_repository COMMAND test_database_repository)`.

- [ ] **Step 3: Run CTest to verify passes**
Run: `cmake --build backend/build && ctest --test-dir backend/build -R test_database_repository --output-on-failure`
Expected: PASS (all assertions succeed).

- [ ] **Step 4: Commit**
Run: `git add backend/tests/unit/test_database_repository.c backend/CMakeLists.txt && git commit -m "test(database): ✅ add dual-engine database and repository unit test suite"`

---

### Task 6: Clean Repository Refactoring: User, Asset, Transaction & Portfolio

**Files:**
- Create: `backend/src/repositories/user_repository.h`
- Create: `backend/src/repositories/user_repository.c`
- Create: `backend/src/repositories/asset_repository.h`
- Create: `backend/src/repositories/asset_repository.c`
- Create: `backend/src/repositories/transaction_repository.h`
- Create: `backend/src/repositories/transaction_repository.c`
- Create: `backend/src/repositories/portfolio_repository.h`
- Create: `backend/src/repositories/portfolio_repository.c`

- [ ] **Step 1: Implement `user_repository`**
Pure data mapping for users table (`user_repo_find_by_id`, `user_repo_find_by_username`, `user_repo_insert`, `user_repo_update_password`). Strip all HTTP context, auth rule decisions, and password hashing (hashing is domain/service).

- [ ] **Step 2: Implement `asset_repository`**
Pure data mapping for assets (`asset_repo_find_by_id`, `asset_repo_list_by_user`, `asset_repo_insert`, `asset_repo_update`). Strip all cost-basis or PnL business math.

- [ ] **Step 3: Implement `transaction_repository`**
Pure data mapping for transactions (`tx_repo_find_by_id`, `tx_repo_list_paged`, `tx_repo_insert`, `tx_repo_delete`). Strip fee-child balance math (balance delta calculation belongs in ledger/financial domain).

- [ ] **Step 4: Implement `portfolio_repository`**
Pure data mapping for portfolios and positions aggregation queries.

- [ ] **Step 5: Verify build & CTest**
Run: `cmake --build backend/build && ctest --test-dir backend/build --output-on-failure`
Expected: PASS (all 25 CTest suites pass).

- [ ] **Step 6: Commit**
Run: `git add backend/src/repositories/user_repository.* backend/src/repositories/asset_repository.* backend/src/repositories/transaction_repository.* backend/src/repositories/portfolio_repository.* && git commit -m "feat(repositories): ✨ implement clean domain repositories without business logic"`

---

### Task 7: Repository Governance & Elimination of `if (database_type == ...)`

**Files:**
- Modify: `backend/src/repositories/price_history_repo.c`
- Modify: `backend/src/repositories/price_history_repo.h`
- Create: `backend/src/repositories/price_history_repository.h`
- Create: `backend/src/repositories/price_history_repository.c`

- [ ] **Step 1: Eliminate engine branch in `price_history_repo.c`**
Replace `if (db_is_postgres())` with unified parameterization or delegate to `price_history_repository.c` using the adapter layer. Zero `if (database_type == ...)` or `if (db_is_postgres())` remains in repository code.

- [ ] **Step 2: Connect legacy repo facades to clean repositories**
Ensure existing repository header signatures continue to function as transparent facades to ensure complete backward compatibility.

- [ ] **Step 3: Run integration test to verify zero regressions**
Run: `cmake --build backend/build && ./backend/tests/test_link.sh`
Expected: PASS (139/139 assertions pass).

- [ ] **Step 4: Commit**
Run: `git add backend/src/repositories/price_history* && git commit -m "refactor(repositories): ♻️ eliminate database engine branching in price history repository"`

---

### Task 8: Full Dual-Engine Verification & Regression Testing

**Files:**
- Test: `backend/tests/unit/test_database_repository.c`
- Integration: `backend/tests/test_link.sh`, `test_ledgers.sh`, `test_2fa.sh`, `test_dca_cashflow.sh`, `test_ai_trace.sh`, `test_market_sync.sh`, `test_fx_oauth.sh`

- [ ] **Step 1: Run all 25 CTest unit test suites**
Run: `ctest --test-dir backend/build --output-on-failure`
Expected: 25/25 PASS.

- [ ] **Step 2: Run all 7 bash integration test suites**
Run:
`./backend/tests/test_link.sh`
`./backend/tests/test_ledgers.sh`
`./backend/tests/test_2fa.sh`
`./backend/tests/test_dca_cashflow.sh`
`./backend/tests/test_ai_trace.sh`
`./backend/tests/test_market_sync.sh`
`./backend/tests/test_fx_oauth.sh`
Expected: ALL PASS.

- [ ] **Step 3: Run frontend build and tests**
Run: `npm --prefix frontend test && npm --prefix frontend run build`
Expected: 25/25 Vitest tests pass; vue-tsc + vite build 0 errors.

- [ ] **Step 4: Commit final changes if any**
Run: `git status`
Expected: Clean working tree.
