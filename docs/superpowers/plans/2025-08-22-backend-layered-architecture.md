# Backend Layered Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate Minefolio backend from flat service layer to three-tier architecture: controllers (HTTP) → services (business) → repositories (data).

**Architecture:** Each domain gets a repository owning all SQL, a service orchestrating business logic + balance ops, and a real controller handling HTTP. Reports stay unchanged.

**Tech Stack:** C23, csilk framework, SQLite, CMake.

**Verification after each phase:**
```bash
cd backend && cmake --build build --parallel 2>&1 | grep -E 'error|Linking|built'
npm --prefix frontend run build 2>&1 | grep -E 'error|✓ built'
```

---

## Pattern Reference (used by all phases)

### Repository pattern
All repos follow this interface. SQL is extracted from the current service; functions take `(pool, user_id, ...)` — never extract user_id from ctx.

```c
// repo.h
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* repo_list(csilk_db_pool_t* pool, int64_t user_id, ...);
csilk_json_t* repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int64_t repo_insert(csilk_db_pool_t* pool, int64_t user_id, ...);
int repo_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, ...);
int repo_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
```

### Service rewrite pattern
Service keeps same public signature `void func(csilk_ctx_t* c)`. Replaces inline SQL with repo calls. Balance/position logic stays in service.

### Controller pattern
Controller includes service header + dtos. Parses body with `csilk_bind_json(c)` for POST/PUT, passes to service. GET endpoints delegate directly. Routes registered via `register_*_routes(app)`.

---

## Phase 1: Tag Domain (simplest, establishes pattern)

### Task 1.1: Create repositories/tag_repo.h

- [ ] Write `backend/src/repositories/tag_repo.h`:
```c
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
csilk_json_t* tag_list(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix);
int64_t tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color);
int tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color);
int tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
```

### Task 1.2: Create repositories/tag_repo.c

- [ ] Write `backend/src/repositories/tag_repo.c`:
```c
#include "repositories/tag_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* tag_list(csilk_db_pool_t* pool, int64_t user_id) {
    char uid[32]; snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    return csilk_db_query_param_json(pool,
        "SELECT id, name, color, created_at FROM tags WHERE user_id=? ORDER BY name",
        (const char*[]){ uid, NULL });
}

csilk_json_t* tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix) {
    char uid[32]; snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    if (!prefix || prefix[0] == '\0') {
        return csilk_db_query_param_json(pool,
            "SELECT id, name, color FROM tags WHERE user_id=? LIMIT 20", (const char*[]){ uid, NULL });
    }
    char pattern[256]; snprintf(pattern, sizeof(pattern), "%%%s%%", prefix);
    return csilk_db_query_param_json(pool,
        "SELECT id, name, color FROM tags WHERE user_id=? AND name LIKE ? LIMIT 10",
        (const char*[]){ uid, pattern, NULL });
}

int64_t tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color) {
    char uid[32]; snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){ uid, name, color ? color : "#666666", NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0)
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}

int tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=?",
        (const char*[]){ name ? name : "", color ? color : "", idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}

int tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "DELETE FROM tags WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
```

### Task 1.3: Rewrite services/tag_service.c

- [ ] Replace entire `backend/src/services/tag_service.c`:
```c
#include "services/tag_service.h"
#include "repositories/tag_repo.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <string.h>

void tags_list(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (id_str) {
        csilk_db_pool_t* pool = db_get_pool();
        char idbuf[32], uidbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%lld", (long long)atoll(id_str));
        snprintf(uidbuf, sizeof(uidbuf), "%lld", (long long)user_id);
        csilk_json_t* res = csilk_db_query_param_json(pool,
            "SELECT id, name, color, created_at FROM tags WHERE id=? AND user_id=?",
            (const char*[]){ idbuf, uidbuf, NULL });
        if (!res || csilk_json_array_size(res) == 0) {
            if (res) csilk_json_free(res);
            respond_not_found(c); return;
        }
        respond_ok(c, res); csilk_json_free(res); return;
    }
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = tag_list(pool, user_id);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result); csilk_json_free(result);
}

void tags_create(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }
    const char* name = csilk_json_get_string(body, "name");
    if (!name || name[0] == '\0') { csilk_json_free(body); respond_bad_request(c, "标签名称不能为空"); return; }
    const char* color = csilk_json_get_string(body, "color");
    csilk_db_pool_t* pool = db_get_pool();
    int64_t id = tag_insert(pool, user_id, name, color);
    csilk_json_free(body);
    if (id <= 0) { respond_error(c, 500, "创建失败"); return; }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_number(resp, "id", (double)id);
    respond_ok(c, resp);
}

void tags_update(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }
    const char* name = csilk_json_get_string(body, "name");
    const char* color = csilk_json_get_string(body, "color");
    csilk_db_pool_t* pool = db_get_pool();
    int ok = tag_update(pool, user_id, atoll(id_str), name, color);
    csilk_json_free(body);
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void tags_delete(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }
    csilk_db_pool_t* pool = db_get_pool();
    int ok = tag_delete(pool, user_id, atoll(id_str));
    if (!ok) { respond_not_found(c); return; }
    respond_ok_null(c);
}

void tags_suggestions(csilk_ctx_t* c) {
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) return;
    const char* q = csilk_get_query(c, "q");
    csilk_db_pool_t* pool = db_get_pool();
    csilk_json_t* result = tag_suggestions(pool, user_id, q);
    if (!result) { respond_error(c, 500, "查询失败"); return; }
    respond_ok(c, result); csilk_json_free(result);
}

void register_tag_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/tags", tags_list, nullptr, "tag_resp_t", "List tags", "Returns all tags for the current user");
    csilk_app_post_ext(app, "/api/tags", tags_create, "tag_req_t", "tag_resp_t", "Create tag", "Create a new tag");
    csilk_app_put_ext(app, "/api/tags/:id", tags_update, "tag_req_t", "tag_resp_t", "Update tag", "Update an existing tag by ID");
    csilk_app_delete_ext(app, "/api/tags/:id", tags_delete, nullptr, nullptr, "Delete tag", "Delete a tag by ID");
    csilk_app_get_ext(app, "/api/tags/suggestions", tags_suggestions, nullptr, "tag_resp_t", "Tag suggestions", "Returns tag suggestions for autocomplete");
}
```

### Task 1.4: Update controllers/tag_controller.c

- [ ] Replace `backend/src/controllers/tag_controller.c`:
```c
#include "controllers/tag_controller.h"
#include "services/tag_service.h"
```
No changes needed — function signatures already match.

### Task 1.5: Build & commit Phase 1

- [ ] Run:
```bash
cd backend && cmake --build build --parallel 2>&1 | grep -E 'error|Linking|built'
npm --prefix frontend run build 2>&1 | grep -E 'error|✓ built'
```
- [ ] Commit:
```bash
git add backend/src/repositories/tag_repo.h backend/src/repositories/tag_repo.c backend/src/services/tag_service.c
git commit -m "refactor(structure): 🏗️ tag domain — first repo/service split"
```

---

## Phase 2: Transfer Domain

### Task 2.1: Create repositories/transfer_repo.h

- [ ] Write `backend/src/repositories/transfer_repo.h`:
```c
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
int transfer_asset_check(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id);
int64_t transfer_insert(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id, double amount, const char* currency, const char* date, const char* note);
```

### Task 2.2: Create repositories/transfer_repo.c

- [ ] Extract SQL from transfer_service.c. Key queries:
  - Asset check: `SELECT COUNT(*) as cnt FROM assets WHERE id IN (?, ?) AND user_id=?`
  - Transfer insert: `INSERT INTO transfers (...) VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id`

### Task 2.3: Rewrite services/transfer_service.c

- [ ] Replace transfer_service.c: call `transfer_asset_check()` and `transfer_insert()` from repo. Keep `balance_apply_delta` calls (lines 107-110 in original) in the service. Keep transaction INSERT for transfer_out/transfer_in as inline SQL (these are business-logic transactions, not pure data access — or extract to repo if preferred; spec says balance logic stays in service, transfer transaction records are also business logic).

### Task 2.4: Update controllers/transfer_controller.c

- [ ] No changes needed — signatures match.

### Task 2.5: Build & commit

---

## Phase 3: Asset Domain

### Task 3.1: Create repositories/asset_repo.h

- [ ] Write with functions: asset_list, asset_get, asset_insert, asset_update, asset_delete, asset_type_check

### Task 3.2: Create repositories/asset_repo.c

- [ ] Extract all 10 SQL queries from asset_service.c:
  - asset_type check, insert, get_by_id, update, delete, list with joins, detail with joins

### Task 3.3: Rewrite services/asset_service.c

- [ ] Replace all `csilk_db_query_param_json` calls with repo calls. Keep `balance_apply_delta` on lines ~260-270.

### Task 3.4: Replace old repositories/asset_repo.c

- [ ] The old `backend/src/repositories/asset_repo.c` (orphaned, only has asset_logs_list) should be removed or merged. Check if it's referenced anywhere:
```bash
grep -r 'asset_repo\|asset_logs' backend/src/ --include='*.c' --include='*.h'
```
If referenced in main.c, update the include. Otherwise delete.

### Task 3.5: Build & commit

---

## Phase 4: Daily Expense Domain

### Task 4.1: Create repositories/daily_expense_repo.h

- [ ] Functions: daily_expense_list, daily_expense_monthly, daily_expense_insert, daily_expense_get, daily_expense_update, daily_expense_delete, expense_tag_insert, expense_tag_delete_all

### Task 4.2: Create repositories/daily_expense_repo.c

- [ ] Extract SQL from daily_expense_query.c (list + monthly) and daily_expense_write.c (create + update + delete + tag ops)

### Task 4.3: Rewrite services/daily_expense_query.c

- [ ] Replace SQL with `daily_expense_list()` and `daily_expense_monthly()` repo calls.

### Task 4.4: Rewrite services/daily_expense_write.c

- [ ] Replace SQL with repo calls. Keep `get_or_create_tag()` static helper and `balance_apply_delta` calls in service.

### Task 4.5: Build & commit

---

## Phase 5: Transaction Domain

### Task 5.1: Create repositories/transaction_repo.h

- [ ] Functions: tx_list, tx_monthly, tx_insert, tx_get, tx_update, tx_delete, asset_exists

### Task 5.2: Create repositories/transaction_repo.c

- [ ] Extract SQL from transaction_query.c (1 query) and transaction_write.c (8 queries). The write side has many parameter combinations — create overloaded-style functions or a single flexible insert.

### Task 5.3: Rewrite services/transaction_query.c

- [ ] Replace with repo calls for list and monthly.

### Task 5.4: Rewrite services/transaction_write.c

- [ ] Replace SQL with repo calls. Keep all `balance_apply_delta`, `apply_position`, `BEGIN/COMMIT/ROLLBACK` logic in service.

### Task 5.5: Build & commit

---

## Phase 6: Category Domain

### Task 6.1: Create repositories/category_repo.h

- [ ] Functions: category_list, category_get, category_insert, category_update, category_delete, category_children, category_find_or_create, category_count

### Task 6.2: Create repositories/category_repo.c

- [ ] Extract all 15 SQL queries from category_service.c

### Task 6.3: Rewrite services/category_service.c

- [ ] Replace CRUD SQL with repo calls. Keep `ensure_default_categories_for_type`, `migrate_legacy_category_names`, `categories_seed_defaults`, `find_or_create_cat` (helper used by seed) in service. These are setup/migration logic, not pure data access.

### Task 6.4: Build & commit

---

## Phase 7: Auth Domain

### Task 7.1: Create repositories/auth_repo.h

- [ ] Functions: user_find_by_username, user_insert, user_get_by_id, user_update_password, user_update_token_version, user_count

### Task 7.2: Create repositories/auth_repo.c

- [ ] Extract SQL from auth_service.c (4 queries) and admin_service.c (1 query)

### Task 7.3: Rewrite services/auth_service.c

- [ ] Replace SQL with repo calls. Keep `store_bcrypt_hash()` static helper and JWT generation in service.

### Task 7.4: Rewrite services/admin_service.c

- [ ] Replace SQL with repo calls. `system_status` and `system_setup` become thin wrappers.

### Task 7.5: Build & commit

---

## Phase 8: Wire Up Controllers in main.c

### Task 8.1: Verify all controllers have real implementations

- [ ] Check each controller .c is more than 1 line:
```bash
for f in backend/src/controllers/*_controller.c; do echo "$(wc -l < $f) $f"; done
```
If any are still 1-line pass-throughs, implement them per the controller pattern.

### Task 8.2: Update main.c includes

- [ ] Replace service includes with controller includes:
```c
// Remove:
#include "services/tag_service.h"
#include "services/transfer_service.h"
#include "services/category_service.h"
#include "services/asset_service.h"
#include "services/transaction_service.h"
#include "services/daily_expense_service.h"
#include "services/auth_service.h"
#include "services/admin_service.h"
#include "services/import_export_service.h"
#include "services/report_service.h"

// Add:
#include "controllers/tag_controller.h"
#include "controllers/transfer_controller.h"
#include "controllers/category_controller.h"
#include "controllers/asset_controller.h"
#include "controllers/transaction_controller.h"
#include "controllers/daily_expense_controller.h"
#include "controllers/auth_controller.h"
#include "controllers/import_export_controller.h"
#include "controllers/report_controller.h"
```

### Task 8.3: Update main.c route registration

- [ ] Function names are the same (`register_*_routes`), so no changes needed to the registration calls. Verify:
```bash
grep 'register_.*_routes' backend/src/main.c
```

### Task 8.4: Remove old orphaned repositories/asset_repo.c

- [ ] Delete if no longer referenced:
```bash
rm backend/src/repositories/asset_repo.c backend/src/repositories/asset_repo.h 2>/dev/null; true
```
(If old asset_repo had unique functions like `asset_logs_list`, move them to the new asset_repo or to a separate logs repo.)

### Task 8.5: Final build & commit

- [ ] Full verification:
```bash
cd backend && cmake --build build --parallel 2>&1 | tail -5
npm --prefix frontend run build 2>&1 | grep -E 'error|✓ built'
git add -A && git commit -m "refactor(structure): 🏗️ complete layered architecture — controllers/services/repositories split"
```

---

## Post-Migration Checklist

- [ ] All services no longer contain raw SQL strings (grep for `csilk_db_query_param_json` in services/ — should be 0 matches except report and import/export which are intentionally left)
- [ ] All repositories contain SQL (grep for `csilk_db_query_param_json` in repositories/ — should match the count from pre-migration)
- [ ] Controllers are non-trivial (each > 1 line)
- [ ] main.c includes controller headers, not service headers (except import_export and report which go through service)
- [ ] No cross-dependency violations (repo never includes service/controller header)
