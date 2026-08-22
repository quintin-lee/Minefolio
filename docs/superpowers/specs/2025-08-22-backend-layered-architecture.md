# Backend Layered Architecture Refactor

## Goal

Transform the current flat service layer into a proper three-tier architecture:
**Controllers (HTTP) → Services (Business) → Repositories (Data)**

## Current State

```
main.c ──────────────────────────────────────────────┐
    │                                               │
    ▼                                               ▼
controllers/  (9 files, all 1-line pass-throughs)   services/  (15 files, do everything)
    │                                               │
    └────────────────────── X ──────────────────────┘
    
repositories/ (1 file, 1 function — orphaned)
models/       (7 structs — defined but unused)
dtos/         (request/response types — unused by services)
```

**Problems:**
- Controllers are dead code — `main.c` never includes them or calls them
- Services contain raw SQL, HTTP parameter parsing, AND business logic
- `balance_apply_delta` calls are scattered across services with no abstraction
- Models/DTOs exist but are never consumed

## Target Architecture

```
main.c
  ├─ middleware registration
  ├─ register_{domain}_routes(app)  ← from controllers
  └─ static file serving

controllers/  — HTTP layer
  ├─ parse request params (csilk_get_param/query/body)
  ├─ call service functions
  └─ format JSON response + send HTTP status

services/     — Business layer
  ├─ orchestrate repo calls
  ├─ handle transactions (BEGIN/COMMIT/ROLLBACK)
  ├─ call balance helpers (balance_apply_delta, apply_position)
  └─ build response csilk_json_t

repositories/ — Data layer
  └─ ALL SQL queries
      ├─ take (pool, user_id, params) → return csilk_json_t or model struct
      └─ zero HTTP knowledge

models/       — Domain structs (existing, minimal changes)
dtos/         — Request/Response types (existing, used by controllers)
```

## Dependency Rules

```
controllers  →  services  →  repositories  →  common/
services     →  repositories  →  common/
controllers  →  services, dtos/
repositories →  common/, models/
```

**No cross-dependencies:**
- repositories MUST NOT include any controller or service headers
- services MUST NOT contain raw SQL strings
- controllers MUST NOT call repositories directly
- main.c MUST only include controller headers for route registration

## Repository Interface Pattern

All repositories follow this pattern:

```c
// repo.h
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include "models/asset.h"

/* Returns csilk_json_t array of row objects (for list queries) */
csilk_json_t* asset_list(csilk_db_pool_t* pool, int64_t user_id,
                          int64_t page, int64_t page_size,
                          const char* filter_type, ...);

/* Returns single row as csilk_json_t, or NULL */
csilk_json_t* asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/* Returns LAST_INSERT_ID via RETURNING, or 0 on failure */
int64_t asset_insert(csilk_db_pool_t* pool, int64_t user_id, ...);

/* Returns rows affected */
int asset_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, ...);

int asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
```

**Key convention:** All repo functions take `(pool, user_id, ...)` — never extract user_id from ctx. This makes repos testable and decoupled from HTTP.

## Service Interface Pattern

Services keep their existing public API (function signatures unchanged):

```c
// Before: void assets_list(csilk_ctx_t* c) { ... SQL ... }
// After:  void assets_list(csilk_ctx_t* c) {
//             int64_t user_id = ctx_user_id(c);
//             if (user_id < 0) return;
//             int64_t page = ..., page_size = ...;
//             parse_page_params(c, &page, &page_size);
//             csilk_json_t* rows = asset_repo_list(pool, user_id, page, page_size, ...);
//             respond_page_ok(c, rows, total, page, page_size);
//         }
```

Services no longer contain SQL strings. They:
1. Extract params from ctx
2. Call repos
3. Apply business rules (balance, position, transactions)
4. Build and send responses

## Controller Implementation Pattern

Controllers become real HTTP handlers:

```c
// Before: #include "services/asset_service.h"  (1 line)
// After:
#include "services/asset_service.h"
#include "dtos/request.h"
#include "dtos/response.h"

void assets_list(csilk_ctx_t* c) {
    asset_service_list(c);  // delegate to service
}

void assets_create(csilk_ctx_t* c) {
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }
    asset_service_create(c, body);
    csilk_json_free(body);
}

void register_asset_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/assets", assets_list, nullptr, ...);
    csilk_app_post_ext(app, "/api/assets", assets_create, "asset_req_t", ...);
    ...
}
```

**Note:** For simple GET endpoints that just pass through, controllers delegate directly.
For POST/PUT with JSON bodies, controllers parse the body and pass it to services.

## Domain-by-Domain Migration Plan

### Phase 1: Simple domains (no balance logic)

| Repo | Service funcs affected | Complexity |
|------|----------------------|------------|
| `tag_repo.c` | tag_service (list/create/update/delete/suggestions) | Low — 6 SQL queries |
| `transfer_repo.c` | transfer_service (create only) | Low — 4 SQL queries |
| `category_repo.c` | category_service (list/create/update/delete/children) | Medium — 16 SQL queries + tree build |

### Phase 2: Balance-linked domains

| Repo | Service funcs affected | Complexity |
|------|----------------------|------------|
| `asset_repo.c` | asset_service (list/create/update/delete/detail) | Low — 12 SQL queries, no balance |
| `daily_expense_repo.c` | daily_expense_query + daily_expense_write | Medium — 20 SQL queries + balance |
| `transaction_repo.c` | transaction_query + transaction_write | High — 11 SQL queries + balance + position |

### Phase 3: Auth domain

| Repo | Service funcs affected | Complexity |
|------|----------------------|------------|
| `auth_repo.c` | auth_service (register/login/change_password/me) + admin_service (status/setup) | Medium — 8 SQL queries + bcrypt + JWT |

## Files Changed Summary

**New files (8 repos + 9 controller updates):**
```
repositories/tag_repo.c/h
repositories/transfer_repo.c/h
repositories/category_repo.c/h
repositories/asset_repo.c/h
repositories/daily_expense_repo.c/h
repositories/transaction_repo.c/h
repositories/auth_repo.c/h
```

**Modified files (9 services + 9 controllers + main.c):**
```
services/tag_service.c/h       → call tag_repo
services/transfer_service.c/h  → call transfer_repo
services/category_service.c/h  → call category_repo
services/asset_service.c/h     → call asset_repo
services/daily_expense_query.c → call daily_expense_repo
services/daily_expense_write.c → call daily_expense_repo
services/transaction_query.c   → call transaction_repo
services/transaction_write.c   → call transaction_repo
services/auth_service.c/h      → call auth_repo
services/admin_service.c/h     → call auth_repo (system functions)
controllers/*.c                → real implementations
main.c                         → include controllers, call controller routes
```

**Deleted files:**
```
repositories/asset_repo.c/h    → replaced by new asset_repo.c/h
```

## Key Design Decisions

1. **No model-to-JSON conversion layer**: Repos return `csilk_json_t*` directly. The existing `db_get_num()`/`csilk_json_get_string()` pattern in services is retained. Adding a model struct conversion layer would double the code without proportional benefit.

2. **Service function signatures unchanged**: Public API (`void func(csilk_ctx_t* c)`) stays the same. This avoids cascading changes to tests and any external callers.

3. **Controller as thin wrapper**: Controllers parse HTTP params and call services. For complex POST bodies, controllers pass the parsed `csilk_json_t*` to services. This keeps the separation clean without over-engineering.

4. **Repository return type**: All repo functions return `csilk_json_t*` (array for lists, single object for gets, int64 for IDs). This matches the existing `csilk_db_query_param_json()` pattern and avoids creating an intermediate conversion layer.

5. **Balance logic stays in services**: `balance_apply_delta` and `apply_position` are service-layer concerns. Repos know nothing about balances.

6. **Report services unchanged**: Report queries are analytical (GROUP BY, aggregates) and don't benefit from the repo pattern. Reports stay as-is.

## Verification

After each phase:
```bash
cmake --build backend/build --parallel && npm --prefix frontend run build
```

Build must pass with zero errors. No test suite changes expected (test_link.sh has pre-existing csilk framework issue).
