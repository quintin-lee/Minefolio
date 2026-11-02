# Transaction Scope & Security Hardening Design

## 1. Context & Motivation

Minefolio is a self-hosted personal wealth and investment management platform written in C23 using the csilk framework and Clean Architecture layering.

During a penetrating full-system code review, six critical issues and stability hazards across transaction management, precision arithmetic, MCP protocol alignment, token replay defense, and POSIX file compliance were identified:
1. **Transaction Boundary Violation (P0)**: Application use cases (`transfer`, `transaction`, `daily_expense`, `dca`, `admin`) directly execute raw SQL string commands (`csilk_db_exec(pool, "BEGIN TRANSACTION/COMMIT/ROLLBACK")`). When nested with internal ledger transactions (`ledger_apply_transfer`), this bypasses the framework's `SAVEPOINT`-based `db_tx_scope_t` abstraction, triggering SQLite `cannot start a transaction within a transaction` errors and prematurely truncating outer transactions.
2. **Non-Atomic Balance Delta Application (P0)**: `balance_apply_delta_m()` performs a 5-step sequence (asset lookup, currency conversion, update, post-balance retrieval, audit log insertion) without an internal transaction scope, risking ledger balance changes with lost audit logs if caller doesn't wrap it in a transaction.
3. **Domain Precision Degradation via Bare Doubles (P0)**: Domain precision rules prohibit bare double arithmetic. However, `asset/usecases.c` calculates investment market values with `new_qty * new_net`, and `market/usecases.c` casts computed 128-bit fixed-point `money_t` to `double` and back, risking IEEE-754 precision drift and ghost audit entries.
4. **JWT Middleware vs. MCP Server Protocol Conflict (P1)**: `mcp_server_controller.c` correctly formats standard JSON-RPC 2.0 error `-32001 (unauthorized)` responses for unauthenticated requests. However, global `jwt_middleware_wrapper` intercepts `/mcp` prior to the handler and aborts with raw HTTP 401, rendering the controller logic dead code and breaking third-party MCP clients like Claude Desktop.
5. **In-Memory Nonce Replay Vulnerability (P1)**: The AI confirmation token subsystem tracks consumed nonces in an in-memory static array `s_consumed_nonces`. Server restarts or container redeployments clear the array, allowing previously consumed tokens to be replayed before their TTL expires.
6. **POSIX `mkstemp` Template Non-Compliance (P1)**: `file_parser.c` constructs upload templates as `/tmp/minefolio_upload_XXXXXX%s` (e.g. `XXXXXX.pdf`). POSIX requires `mkstemp` templates to end strictly with six `XXXXXX` characters; on musl libc and strict glibc systems, this returns `-1 (EINVAL)`, causing file parsing to fail consistently.

This design document establishes the architectural specification to address all six issues comprehensively without external dependencies.

---

## 2. Architecture & Subsystem Topology

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Application Use Cases                             │
│       (transfer, transaction, daily_expense, dca, asset, market, admin)     │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │
      ┌────────────────────────────────┴────────────────────────────────┐
      ▼                                                                 ▼
┌──────────────────────────────┐                      ┌──────────────────────────────┐
│  db_tx_scope_begin / commit  │                      │  128-bit Fixed-Point Types   │
│  (SAVEPOINT-based nesting)   │                      │  (quantity_t * price_t)      │
└──────────────┬───────────────┘                      └──────────────┬───────────────┘
               │                                                     │
               ▼                                                     ▼
┌──────────────────────────────┐                      ┌──────────────────────────────┐
│   Core Ledger Engine         │                      │  balance_apply_delta_m()     │
│   (ledger_apply_tx, etc.)    │                      │  (Self-contained tx scope)   │
└──────────────┬───────────────┘                      └──────────────┬───────────────┘
               │                                                     │
               └───────────────────────┬─────────────────────────────┘
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                 Database & Infrastructure Layer (SQLite / PG)               │
│ - Safe nested transactions via SAVEPOINT rollback / release                 │
│ - Persistent ai_consumed_nonces table (V019 migration)                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                       ▲
                                       │
┌──────────────────────────────┐       │              ┌──────────────────────────────┐
│  JWT Middleware              │       │              │  AI Policy Confirmation      │
│  (Loose /mcp pass-through)   │───────┼─────────────►│  (Dual-layer Nonce Defense:  │
│  (Controller returns -32001) │       │              │   Fast LRU + Persistent DB)  │
└──────────────────────────────┘       │              └──────────────────────────────┘
                                       │
┌──────────────────────────────┐       │
│  file_parser (mkstemps)      │───────┘
│  (POSIX compliant template)  │
└──────────────────────────────┘
```

---

## 3. Detailed Specifications

### 3.1 Unified Transaction Scope (`db_tx_scope_t`) & `balance_apply_delta_m` Atomic Guard

#### Use Case Transaction Migration
All application use cases replace raw string execution with `db_tx_scope_t`:
1. `src/application/transfer/usecases.c`: replace `csilk_db_exec(pool, "BEGIN TRANSACTION")` and `"COMMIT"` / `"ROLLBACK"` with `db_tx_scope_begin(pool, "mf_transfer_create", &scope)`.
2. `src/application/transaction/usecases.c`: replace all 4 transaction boundaries with `db_tx_scope_begin(pool, "mf_tx_scope", &scope)`.
3. `src/application/daily_expense/usecases.c`: replace all 3 transaction boundaries with `db_tx_scope_begin(pool, "mf_expense_scope", &scope)`.
4. `src/application/dca/usecases.c`: replace raw transaction calls with `db_tx_scope_begin(pool, "mf_dca_scope", &scope)`.
5. `src/interfaces/http/controllers/admin_controller.c`: replace system setup transaction with `db_tx_scope_begin(pool, "mf_setup_scope", &scope)`.
6. `src/infrastructure/repositories/mcp_server_repo_impl.c`: replace tool refresh transaction with `db_tx_scope_begin(pool, "mf_mcp_tools_refresh", &scope)`.

#### Self-Contained Atomic Balance Delta Application
In `src/common/balance.c`, `balance_apply_delta_m()` wraps its internal execution in an autonomous transaction scope:
```c
int balance_apply_delta_m(csilk_db_pool_t* pool, int64_t asset_id, int64_t user_id,
                          money_t delta, const char* source_type, int64_t source_id,
                          const char* note)
{
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) return -1;
    
    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_balance_delta", &scope) != 0) {
        return -2;
    }
    
    /* 1. Fetch asset currency and type */
    ...
    /* 2. Normalize liability delta */
    ...
    /* 3. Atomic UPDATE current_value */
    if (!res3) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    /* 4. Read balance_after snapshot */
    if (!after || csilk_json_array_size(after) == 0) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    /* 5. Insert audit log */
    if (!res5 || csilk_json_array_size(res5) == 0) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    return 0;
}
```
Because `db_tx_scope_begin` automatically resolves to a `SAVEPOINT` when an outer transaction is active, this guarantees atomicity when called standalone without interfering with parent transactions.

---

### 3.2 Domain Strong-Typing & Elimination of Bare Doubles

#### Asset Position Update (`src/application/asset/usecases.c`)
Replace float arithmetic with 128-bit decimal fixed-point routines:
```c
/* Construct strong-typed inputs */
quantity_t qty_obj;
quantity_from_double(new_qty, 4, &qty_obj);

price_t net_obj;
price_from_double(new_net, 4, asset_currency, &net_obj);

/* Multiply: money_t (2 decimal places) = quantity (4 places) * price (4 places) */
money_t new_val_m;
if (money_from_quantity_and_price(qty_obj, net_obj, &new_val_m) != DECIMAL_OK) {
    db_tx_scope_rollback(db_pool, &scope);
    out_res->code = 1004;
    snprintf(out_res->message, sizeof(out_res->message), "金额计算溢出");
    return -1;
}

money_t old_val_m;
money_from_double(old_current, asset_currency, &old_val_m);

money_t delta_m = money_sub(new_val_m, old_val_m);

if (!money_is_zero(delta_m)) {
    int b_rc = balance_apply_delta_m(db_pool, cmd->id, cmd->user_id, delta_m,
                                     "asset_netvalue", cmd->id, "net_value update");
    if (b_rc != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        out_res->code = (b_rc == -1) ? 1002 : 1004;
        snprintf(out_res->message, sizeof(out_res->message), "更新资产余额失败");
        return -1;
    }
}
```

#### Market Quote Sync (`src/application/market/usecases.c`)
Eliminate double conversion round-trip:
```c
money_t delta_m;
mf_market_rule_calc_sync_delta(p_old, p_new, qty, cur, &delta_m);

if (!money_is_zero(delta_m)) {
    if (balance_apply_delta_m(db_pool, aid, uid, delta_m,
                              "asset_market_sync", aid, "Market quote sync") != 0) {
        write_ok = 0;
    }
}
```

---

### 3.3 MCP Endpoint Optional Auth & JSON-RPC Protocol Alignment

#### Middleware Update (`src/middlewares/jwt_middleware.c`)
In `jwt_middleware_wrapper()`:
```c
const char* path = csilk_get_path(c);
bool is_mcp_path = (path && strcmp(path, "/mcp") == 0);

const char* auth_header = csilk_get_header(c, "Authorization");
if (!auth_header || strncmp(auth_header, "Bearer ", 7) != 0) {
    if (is_mcp_path) {
        /* Allow unauthenticated request to reach mcp_endpoint_handler
         * which emits standard JSON-RPC 2.0 error code -32001 */
        csilk_next(c);
        return;
    }
    csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Bearer token required");
    csilk_abort(c);
    return;
}
```
If an invalid token is provided on `/mcp`, the middleware checks `is_mcp_path` and similarly forwards to the controller to emit `-32001` instead of an HTTP 401 status.

---

### 3.4 Persistent AI Confirmation Nonce Defense (V019 Migration)

#### Schema Definition (`V019__ai_consumed_nonces.sql`)
In `backend/sql/migrations/sqlite/V019__ai_consumed_nonces.sql` and `backend/sql/migrations/postgres/V019__ai_consumed_nonces.sql`:
```sql
CREATE TABLE IF NOT EXISTS ai_consumed_nonces (
    nonce       VARCHAR(64) PRIMARY KEY,
    expiration  BIGINT NOT NULL,
    user_id     BIGINT NOT NULL,
    consumed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ai_consumed_nonces_exp ON ai_consumed_nonces(expiration);
```

#### Verification & Consumption Routine (`src/services/ai/policy/confirmation.c`)
1. **Fast-path Check**: Look up `nonce` in memory ring `s_consumed_nonces`. If found and `expiration >= now`, reject as duplicate.
2. **Persistent Check**:
   - Execute: `SELECT 1 FROM ai_consumed_nonces WHERE nonce = ? AND expiration >= ?`.
   - If row exists: populate memory LRU cache and reject as duplicate.
3. **Consumption Action**:
   - Insert into database:
     `INSERT INTO ai_consumed_nonces (nonce, expiration, user_id) VALUES (?, ?, ?)`.
   - Update memory LRU cache `record_nonce_consumed_locked()`.
   - Low-frequency garbage collection: on every 10th write, execute:
     `DELETE FROM ai_consumed_nonces WHERE expiration < (now - 300)`.

---

### 3.5 POSIX `mkstemps` File Creation Compliance (`src/services/file_parser.c`)

Replace `snprintf(...) + mkstemp()` with `mkstemps()`:
```c
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <unistd.h>

static int write_temp_file(const char* data, size_t len, const char* suffix,
                           char* out_path, size_t out_path_len)
{
    if (!suffix) suffix = "";
    size_t suffix_len = strlen(suffix);
    
    if (snprintf(out_path, out_path_len, "/tmp/minefolio_upload_XXXXXX%s", suffix) >= (int)out_path_len) {
        return -1;
    }
    
    int fd = mkstemps(out_path, (int)suffix_len);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}
```

---

## 4. Error Handling & Security Matrix

| Hazard Condition | Detection Point | Prevention / Recovery Strategy |
| :--- | :--- | :--- |
| Nested transaction in `transfer/usecases.c` | `db_tx_scope_begin()` | Automatically uses `SAVEPOINT mf_transfer_create`; no SQLite error |
| Partial failure during `balance_apply_delta_m` | `db_tx_scope_rollback()` | Internal scope rolls back `current_value` update and audit log together |
| Floating-point precision error on position update | `decimal_mul()` | Exact 128-bit integer mantissa multiplication; zero ghost diffs |
| Unauthenticated Claude Desktop accessing `/mcp` | `jwt_middleware.c` | Passes request through to handler; emits JSON-RPC `-32001` with HTTP 200 |
| Replay attack on draft token after server restart | `ai_consumed_nonces` table | Database lookup identifies previously consumed nonce; rejects replay |
| PDF upload on musl/Alpine environment | `mkstemps()` with suffix length | Complies with POSIX.1-2008; eliminates `EINVAL` template failures |

---

## 5. Verification & Testing Strategy

### 5.1 Unit Tests & CMake Targets
1. **`test_tx_scope_nesting.c`**:
   - Begin outer `db_tx_scope_begin()`.
   - Call inner `balance_apply_delta_m()` and use case functions.
   - Assert all nested savepoints commit cleanly without SQLite errors.
   - Trigger inner rollback; assert outer transaction remains valid and recoverable.
2. **`test_financial_precision_edge.c`**:
   - Set price to `19.99` and quantity to `1000.0000`.
   - Execute position update and market sync.
   - Assert resulting balance change is exactly `19990.00` without sub-cent float residuals.
3. **`test_mcp_unauth_protocol.c`**:
   - Send `POST /mcp` without `Authorization` header.
   - Assert HTTP status code is `200 OK`.
   - Assert response body is valid JSON-RPC with `error.code == -32001`.
4. **`test_nonce_persistence.c`**:
   - Verify and consume draft token.
   - Clear memory cache `s_consumed_nonces`.
   - Attempt to verify and consume same token again.
   - Assert verification fails with `ERR_REPLAY` via database lookup.
5. **`test_file_parser_mkstemps.c`**:
   - Invoke `write_temp_file` with `.pdf` and `.zip` suffixes.
   - Assert file is created, written, closed, and deleted cleanly with valid file descriptors.

### 5.2 Regression Testing
- Run full CTest suite (`ctest --test-dir build --output-on-failure`).
- Execute integration scripts:
  - `bash tests/test_link.sh`
  - `bash tests/test_mcp.sh`
  - `bash tests/test_ai_loop_hardening.sh`
