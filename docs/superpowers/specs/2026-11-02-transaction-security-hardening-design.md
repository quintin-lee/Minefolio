# Transaction Scope & Security Hardening Design

## 1. Context & Motivation

Minefolio is a self-hosted personal wealth and investment management platform written in C23 using the csilk framework and Clean Architecture layering.

During a penetrating full-system code review, six critical issues and stability hazards across transaction management, precision arithmetic, MCP protocol alignment, token replay defense, and POSIX file compliance were identified:
1. **Transaction Boundary Violation (P0)**: Application use cases (`transfer`, `transaction`, `daily_expense`, `dca`, `admin`, `mcp_server_repo`) directly execute raw SQL string commands (`csilk_db_exec(pool, "BEGIN TRANSACTION/COMMIT/ROLLBACK")`). When nested with internal ledger transactions (`ledger_apply_transfer`), this bypasses the framework's `SAVEPOINT`-based `db_tx_scope_t` abstraction, triggering SQLite `cannot start a transaction within a transaction` errors and prematurely truncating outer transactions.
2. **Non-Atomic Balance Delta Application (P0)**: `balance_apply_delta_m()` performs a 5-step sequence (asset lookup, currency conversion, update, post-balance retrieval, audit log insertion) without an internal transaction scope, risking ledger balance changes with lost audit logs if caller doesn't wrap it in a transaction.
3. **Domain Precision Degradation via Bare Doubles (P0)**: Domain precision rules prohibit bare double arithmetic. However, `asset/usecases.c` calculates investment market values with `new_qty * new_net`, and `market/usecases.c` casts computed 128-bit fixed-point `money_t` to `double` and back across both batch and single-asset sync paths, risking IEEE-754 precision drift and ghost audit entries.
4. **JWT Middleware vs. MCP Server Protocol Conflict (P1)**: `mcp_server_controller.c` correctly formats standard JSON-RPC 2.0 error `-32001 (unauthorized)` responses for unauthenticated requests. However, global `jwt_middleware_wrapper` intercepts `/mcp` prior to the handler and aborts with raw HTTP 401. Furthermore, `ctx_user_id()` automatically emits Minefolio HTTP envelope errors (`{"code": 1001, ...}`) on unauthenticated requests. Both issues render controller JSON-RPC responses dead code and break third-party MCP clients like Claude Desktop.
5. **In-Memory Nonce Replay Vulnerability (P1)**: The AI confirmation token subsystem tracks consumed nonces in an in-memory static ring array `s_consumed_nonces`. Server restarts or container redeployments clear the array, allowing previously consumed tokens to be replayed before their TTL expires.
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
│  (SAVEPOINT-based nesting)   │                      │  (price_times_quantity)      │
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
│  (jwt_get_user_id: -32001)   │       │              │   Fast Ring + Persistent DB) │
└──────────────────────────────┘       │              └──────────────────────────────┘
                                       │
┌──────────────────────────────┐       │
│  file_parser (mkstemps)      │───────┘
│  (Portable BSD/GNU extension)│
└──────────────────────────────┘
```

---

## 3. Detailed Specifications

### 3.1 Unified Transaction Scope (`db_tx_scope_t`) & `balance_apply_delta_m` Atomic Guard

#### Use Case Transaction Migration
All application use cases replace raw string execution with `db_tx_scope_t`:
1. `src/application/transfer/usecases.c`: replace `csilk_db_exec(pool, "BEGIN TRANSACTION")` and `"COMMIT"` / `"ROLLBACK"` (lines 49, 131) with `db_tx_scope_begin(pool, "mf_transfer_create", &scope)`.
2. `src/application/transaction/usecases.c`: replace all 3 transaction boundaries with `db_tx_scope_begin(pool, "mf_tx_scope", &scope)`:
   - `transaction_usecase_create` (line 108)
   - `transaction_usecase_delete` (line 233)
   - `transaction_usecase_update` (line 306)
3. `src/application/daily_expense/usecases.c`: replace all 3 transaction boundaries with `db_tx_scope_begin(pool, "mf_expense_scope", &scope)`:
   - `daily_expense_usecase_create` (line 375)
   - `daily_expense_usecase_update` (line 467)
   - `daily_expense_usecase_delete` (line 555)
4. `src/application/dca/usecases.c`: replace raw transaction calls with `db_tx_scope_begin(pool, "mf_dca_scope", &scope)` (lines 249, 283).
5. `src/interfaces/http/controllers/admin_controller.c`: replace system setup transaction with `db_tx_scope_begin(pool, "mf_setup_scope", &scope)` (lines 115, 161).
6. `src/infrastructure/repositories/mcp_server_repo_impl.c`: replace tool refresh transaction with `db_tx_scope_begin(pool, "mf_mcp_tools_refresh", &scope)` (lines 367, 409).

#### Self-Contained Atomic Balance Delta Application
In `src/common/balance.c`, `balance_apply_delta_m()` wraps its internal execution in an autonomous transaction scope with complete error cleanup:
```c
int
balance_apply_delta_m(csilk_db_pool_t* pool,
                      int64_t          asset_id,
                      int64_t          user_id,
                      money_t          delta,
                      const char*      source_type,
                      int64_t          source_id,
                      const char*      note)
{
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) {
        return -1;
    }

    db_tx_scope_t scope;
    if (db_tx_scope_begin(pool, "mf_balance_delta", &scope) != 0) {
        return -2;
    }

    char asset_id_str[32], user_id_str[32];
    snprintf(asset_id_str, sizeof(asset_id_str), "%lld", (long long)asset_id);
    snprintf(user_id_str, sizeof(user_id_str), "%lld", (long long)user_id);

    // 1. 查询资产归属与类型
    const char*   params1[] = {asset_id_str, user_id_str, NULL};
    csilk_json_t* row = csilk_db_query_param_json(
        pool,
        "SELECT a.current_value, a.currency, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.id=? AND a.user_id=?",
        params1);
    if (!row) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    if (csilk_json_array_size(row) == 0) {
        csilk_json_free(row);
        CSILK_LOG_W("balance_apply_delta: asset not found asset_id=%lld user_id=%lld",
                    (long long)asset_id,
                    (long long)user_id);
        db_tx_scope_rollback(pool, &scope);
        return -1;
    }
    const csilk_json_t* asset = csilk_json_array_get(row, 0);
    const char*         asset_type = csilk_json_get_string(asset, "asset_type");
    const char*         asset_cur_str = csilk_json_get_string(asset, "currency");
    currency_t          asset_cur = currency_from_str(asset_cur_str);

    /* Multi-currency normalization: convert delta to the asset's native currency if different */
    money_t target_delta = delta;
    if (!currency_equals(delta.currency, asset_cur)) {
        target_delta = exchange_rate_convert_m(delta, asset_cur);
    }

    // 2. Normalize delta (liability balance direction flip)
    money_t signed_delta = target_delta;
    if (balance_direction(asset_type) < 0) {
        signed_delta = money_neg(target_delta);
    }
    csilk_json_free(row);

    // 3. 原子更新余额（定点数字符串格式化）
    char signed_delta_str[64];
    money_to_string(signed_delta, signed_delta_str, sizeof(signed_delta_str));

    const char*   params3[] = {signed_delta_str, asset_id_str, user_id_str, NULL};
    csilk_json_t* res3 = csilk_db_query_param_json(
        pool,
        "UPDATE assets SET current_value = current_value + ?, "
        "updated_at = CURRENT_TIMESTAMP WHERE id=? AND user_id=?",
        params3);
    if (!res3) {
        CSILK_LOG_E("balance_apply_delta: failed to update balance asset_id=%lld",
                    (long long)asset_id);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    csilk_json_free(res3);

    // 4. 读取变动后余额（balance_after 快照）
    const char*   params4[] = {asset_id_str, user_id_str, NULL};
    csilk_json_t* after = csilk_db_query_param_json(
        pool, "SELECT current_value FROM assets WHERE id=? AND user_id=?", params4);
    if (!after || csilk_json_array_size(after) == 0) {
        if (after) {
            csilk_json_free(after);
        }
        CSILK_LOG_E("balance_apply_delta: failed to read balance_after asset_id=%lld",
                    (long long)asset_id);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    money_t balance_after =
        db_get_money(csilk_json_array_get(after, 0), "current_value", asset_cur);
    csilk_json_free(after);

    // 5. 写审计日志
    char balance_after_str[64], source_id_str[32];
    money_to_string(balance_after, balance_after_str, sizeof(balance_after_str));
    snprintf(source_id_str, sizeof(source_id_str), "%lld", (long long)source_id);

    const char*   params5[] = {asset_id_str,
                               user_id_str,
                               signed_delta_str,
                               balance_after_str,
                               source_type,
                               source_id_str,
                               note ? note : "",
                               NULL};
    csilk_json_t* res5 = csilk_db_query_param_json(
        pool,
        "INSERT INTO balance_audit_logs "
        "(asset_id, user_id, delta, balance_after, source_type, source_id, note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        params5);
    /* INSERT without RETURNING returns non-null json object/array with size 0; check !res5 */
    if (!res5) {
        CSILK_LOG_E("balance_apply_delta: failed to insert audit log asset_id=%lld",
                    (long long)asset_id);
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    csilk_json_free(res5);

    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        return -2;
    }
    return 0;
}
```

---

### 3.2 Domain Strong-Typing & Elimination of Bare Doubles

#### Asset Position Update (`src/application/asset/usecases.c`)
Replace float arithmetic with 128-bit decimal fixed-point routines:
```c
/* Pre-compute values before starting the transaction scope */
currency_t asset_currency = currency_from_str(currency);

quantity_t qty_obj;
quantity_from_double(new_qty, 4, &qty_obj);

price_t net_obj;
price_from_double(new_net, 4, asset_currency, &net_obj);

/* Multiply: money_t (2 decimal places) = price_times_quantity(net_obj, qty_obj, &new_val_m) */
money_t new_val_m;
if (price_times_quantity(net_obj, qty_obj, &new_val_m) != DECIMAL_OK) {
    csilk_json_free(cur);
    out_res->code = 1004;
    snprintf(out_res->message, sizeof(out_res->message), "金额计算溢出");
    return -1;
}

money_t old_val_m;
money_from_double(old_current, asset_currency, &old_val_m);

money_t delta_m = money_sub(new_val_m, old_val_m);

db_tx_scope_t scope;
if (db_tx_scope_begin(db_pool, "mf_asset_update_investment", &scope) != 0) {
    csilk_json_free(cur);
    out_res->code = 1004;
    snprintf(out_res->message, sizeof(out_res->message), "数据库错误");
    return -1;
}

if (!money_is_zero(delta_m)) {
    int b_rc = balance_apply_delta_m(db_pool,
                                     cmd->id,
                                     cmd->user_id,
                                     delta_m,
                                     "asset_netvalue",
                                     cmd->id,
                                     "net_value update");
    if (b_rc != 0) {
        db_tx_scope_rollback(db_pool, &scope);
        csilk_json_free(cur);
        out_res->code = (b_rc == -1) ? 1002 : 1004;
        snprintf(out_res->message, sizeof(out_res->message), "更新资产余额失败");
        return -1;
    }
}

double updated_current = money_to_double(new_val_m);
if (!asset_update_position(db_pool, cmd->user_id, cmd->id, new_net, new_qty, new_cost) ||
    !asset_update_basic(db_pool, cmd->user_id, cmd->id, name, account_no,
                        updated_current, account_id_val, note)) {
    db_tx_scope_rollback(db_pool, &scope);
    csilk_json_free(cur);
    out_res->code = 1004;
    snprintf(out_res->message, sizeof(out_res->message), "更新资产失败");
    return -1;
}

if (db_tx_scope_commit(db_pool, &scope) != 0) {
    db_tx_scope_rollback(db_pool, &scope);
    csilk_json_free(cur);
    out_res->code = 1004;
    snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
    return -1;
}
```

#### Market Quote Sync (`src/application/market/usecases.c`)
Eliminate double conversion round-trip in both batch sync (`market_service_sync_quotes`) and single asset sync (`mf_market_sync_single_asset`):

1. **Batch Sync (lines 157–170)**:
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

2. **Single Asset Sync (lines 313–328)**:
```c
money_t delta_m;
mf_market_rule_calc_sync_delta(p_old, p_new, qty, cur, &delta_m);

if (!money_is_zero(delta_m)) {
    if (balance_apply_delta_m(pool, asset_id, user_id, delta_m,
                              "asset_market_sync", asset_id, "Market quote sync") != 0) {
        write_ok = 0;
    }
}
```

---

### 3.3 MCP Endpoint Optional Auth & JSON-RPC Protocol Alignment

#### 1. Middleware Update (`src/middlewares/jwt_middleware.c`)
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

If a Bearer token is provided but fails verification (e.g. invalid signature, expired, or version mismatch):
```c
if (!payload) {
    if (is_mcp_path) {
        csilk_next(c);
        return;
    }
    csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid or expired token");
    csilk_abort(c);
    return;
}
```

#### 2. Non-Intrusive User ID Extraction in MCP Controller & Rate Limiter
In `src/interfaces/http/controllers/mcp_server_controller.c:176` and `src/main.c:mcp_rate_limit_middleware`:
Replace `ctx_user_id(c)` with `jwt_get_user_id(c)`:
- `ctx_user_id(c)` triggers `respond_unauthorized(c)` writing HTTP 200 `{"code":1001,"message":"未授权"}` on failure.
- `jwt_get_user_id(c)` safely returns `-1` without side effects or HTTP writes when unauthenticated.
```c
/* In mcp_server_controller.c:mcp_endpoint_handler */
int64_t user_id = jwt_get_user_id(c);
if (user_id < 0) {
    /* 未认证：JSON-RPC error -32001（unauthorized），HTTP 仍 200 承载 JSON-RPC 体 */
    csilk_json_t* body = csilk_bind_json(c);
    mcp_send_response(c, body ? mcp_req_id(body) : NULL, NULL, -32001, "unauthorized");
    csilk_json_free(body);
    return;
}
```

In `src/main.c:mcp_rate_limit_middleware`:
```c
int64_t uid = jwt_get_user_id(c);
if (uid <= 0) {
    /* Unauthenticated MCP requests pass through to mcp_endpoint_handler */
    csilk_next(c);
    return;
}
```

---

### 3.4 Persistent AI Confirmation Nonce Defense (V019 Migration)

#### Schema Definition (`sql/migrations/sqlite/V019__ai_consumed_nonces.sql`)
In `sql/migrations/sqlite/V019__ai_consumed_nonces.sql` and `sql/migrations/postgres/V019__ai_consumed_nonces.sql`:
```sql
CREATE TABLE IF NOT EXISTS ai_consumed_nonces (
    nonce       VARCHAR(64) PRIMARY KEY,
    expiration  BIGINT NOT NULL,
    user_id     BIGINT NOT NULL,
    consumed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ai_consumed_nonces_exp ON ai_consumed_nonces(expiration);
```

#### Dual-Layer Ring Cache & DB Verification (`src/services/ai/policy/confirmation.c`)
In `ai_confirmation_verify_and_consume()`:
```c
/* 1. Fast-path: check in-memory ring buffer */
pthread_mutex_lock(&s_nonce_lock);
for (size_t i = 0; i < s_nonce_count; i++) {
    if (strcmp(s_consumed_nonces[i].nonce, nonce) == 0 &&
        s_consumed_nonces[i].expiration >= now) {
        pthread_mutex_unlock(&s_nonce_lock);
        return AI_CONFIRM_ERR_REPLAY;
    }
}
pthread_mutex_unlock(&s_nonce_lock);

/* 2. Persistent check: verify database if db_get_pool() is initialized */
csilk_db_pool_t* pool = db_get_pool();
if (pool) {
    char now_str[32];
    snprintf(now_str, sizeof(now_str), "%lld", (long long)now);
    const char* params_chk[] = {nonce, now_str, NULL};
    csilk_json_t* res_chk = csilk_db_query_param_json(
        pool,
        "SELECT 1 FROM ai_consumed_nonces WHERE nonce = ? AND expiration >= ?",
        params_chk);
    if (res_chk && csilk_json_array_size(res_chk) > 0) {
        csilk_json_free(res_chk);
        /* Backfill memory ring */
        pthread_mutex_lock(&s_nonce_lock);
        record_nonce_consumed_locked(nonce, exp);
        pthread_mutex_unlock(&s_nonce_lock);
        return AI_CONFIRM_ERR_REPLAY;
    }
    if (res_chk) {
        csilk_json_free(res_chk);
    }
}

/* 3. Execute database consumption */
if (pool) {
    char exp_str[32], uid_str[32];
    snprintf(exp_str, sizeof(exp_str), "%lld", (long long)exp);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params_ins[] = {nonce, exp_str, uid_str, NULL};
    csilk_json_t* res_ins = csilk_db_query_param_json(
        pool,
        "INSERT INTO ai_consumed_nonces (nonce, expiration, user_id) VALUES (?, ?, ?)",
        params_ins);
    if (!res_ins) {
        /* Primary key collision indicates concurrent replay */
        return AI_CONFIRM_ERR_REPLAY;
    }
    csilk_json_free(res_ins);

    /* Low-frequency background purge of expired nonces (1 in 10 writes) */
    static _Atomic uint32_t s_gc_counter = 0;
    if ((++s_gc_counter % 10) == 0) {
        char old_str[32];
        snprintf(old_str, sizeof(old_str), "%lld", (long long)(now - 300));
        csilk_json_t* res_del = csilk_db_query_param_json(
            pool, "DELETE FROM ai_consumed_nonces WHERE expiration < ?", (const char*[]){old_str, NULL});
        if (res_del) csilk_json_free(res_del);
    }
}

/* 4. Update memory ring buffer */
pthread_mutex_lock(&s_nonce_lock);
record_nonce_consumed_locked(nonce, exp);
pthread_mutex_unlock(&s_nonce_lock);
```

---

### 3.5 POSIX `mkstemps` File Creation Compliance (`src/services/file_parser.c`)

Replace `snprintf(...) + mkstemp()` with `mkstemps()`:
```c
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <unistd.h>

static int
write_temp_file(const char* data, size_t len, const char* suffix,
                char* out_path, size_t out_path_len)
{
    if (!suffix) suffix = "";
    size_t suffix_len = strlen(suffix);

    if (snprintf(out_path, out_path_len, "/tmp/minefolio_upload_XXXXXX%s", suffix) >= (int)out_path_len) {
        return -1;
    }

    /* mkstemps is supported by glibc/musl; takes length of suffix to keep XXXXXX replaceable */
    int fd = mkstemps(out_path, (int)suffix_len);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    if (n != (ssize_t)len) {
        unlink(out_path);
        return -1;
    }
    return 0;
}
```

---

## 4. Error Handling & Security Matrix

| Hazard Condition | Detection Point | Prevention / Recovery Strategy |
| :--- | :--- | :--- |
| Nested transaction in `transfer/usecases.c` | `db_tx_scope_begin()` | Automatically uses `SAVEPOINT mf_transfer_create`; no SQLite error |
| Partial failure during `balance_apply_delta_m` | `db_tx_scope_rollback()` | Internal scope rolls back `current_value` update and audit log together |
| Floating-point precision error on position update | `price_times_quantity()` | Exact 128-bit integer mantissa multiplication; zero ghost diffs |
| Unauthenticated Claude Desktop accessing `/mcp` | `jwt_middleware.c` + `jwt_get_user_id` | Passes request through to handler; emits JSON-RPC `-32001` with HTTP 200 |
| Replay attack on draft token after server restart | `ai_consumed_nonces` table | Database lookup identifies previously consumed nonce; rejects replay |
| Concurrent replay of same draft token | `INSERT INTO ai_consumed_nonces` | Database PRIMARY KEY violation safely caught and mapped to `AI_CONFIRM_ERR_REPLAY` |
| PDF upload on musl/Alpine environment | `mkstemps()` with suffix length | Eliminates `EINVAL` template failures; unlinks file on write failure |

---

## 5. Verification & Testing Strategy

### 5.1 Unit Tests & CMake Targets
The following new unit test targets are registered under `CMakeLists.txt` via `add_executable()` and `add_test()`:
1. **`tests/unit/test_tx_scope_nesting.c`**:
   - Begin outer `db_tx_scope_begin()`.
   - Call inner `balance_apply_delta_m()` and use case functions.
   - Assert all nested savepoints commit cleanly without SQLite errors.
   - Trigger inner rollback; assert outer transaction remains valid and recoverable.
2. **`tests/unit/test_financial_precision_edge.c`**:
   - Set price to `19.99` and quantity to `1000.0000`.
   - Execute position update and market sync.
   - Assert resulting balance change is exactly `19990.00` without sub-cent float residuals.
3. **`tests/unit/test_mcp_unauth_protocol.c`**:
   - Send `POST /mcp` without `Authorization` header.
   - Assert HTTP status code is `200 OK`.
   - Assert response body is valid JSON-RPC with `error.code == -32001`.
4. **`tests/unit/test_nonce_persistence.c`**:
   - Verify and consume draft token.
   - Clear memory cache `s_consumed_nonces`.
   - Attempt to verify and consume same token again.
   - Assert verification fails with `AI_CONFIRM_ERR_REPLAY` via database lookup.
5. **`tests/unit/test_file_parser_mkstemps.c`**:
   - Invoke `write_temp_file` with `.pdf` and `.zip` suffixes.
   - Assert file is created, written, closed, and deleted cleanly with valid file descriptors.

### 5.2 Regression Testing
- Run full CTest suite (`ctest --test-dir build --output-on-failure`).
- Execute integration scripts:
  - `bash tests/test_link.sh`
  - `bash tests/test_mcp.sh`
  - `bash tests/test_mcp_server.sh`
  - `bash tests/test_ai_loop_hardening.sh`
