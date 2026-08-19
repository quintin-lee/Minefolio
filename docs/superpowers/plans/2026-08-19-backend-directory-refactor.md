# Backend 目录结构重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `backend/src/` 从扁平结构重构为 controllers/services/repositories/models/dtos/middlewares/config 分层架构，并拆分过大的源文件。

**Architecture:** 三层架构（controller → service → repository），每域独立子目录。models/ 定义表映射 struct，dtos/ 定义请求/响应 DTO。中间件从 main.c 提取为独立模块。

**Tech Stack:** C23, csilk HTTP 框架, SQLite/PostgreSQL, CMake

---

## 实施前准备

- [ ] **Step 0: 保存当前状态**

```bash
git branch refactor/before-structure
git status
```

确认工作树干净后再开始。

---

## Task 1: 创建目录结构与公共头文件

**Files:**
- Create: `backend/src/models/user.h`
- Create: `backend/src/models/category.h`
- Create: `backend/src/models/asset.h`
- Create: `backend/src/models/transaction.h`
- Create: `backend/src/models/daily_expense.h`
- Create: `backend/src/models/tag.h`
- Create: `backend/src/models/transfer.h`
- Create: `backend/src/dtos/request.h`
- Create: `backend/src/dtos/response.h`

- [ ] **Step 1: 创建所有 models/*.h**

根据各表字段定义 struct，字段名与 JSON key 保持一致：

```c
// backend/src/models/user.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    char     username[128];
    char     password[256];
    char     created_at[64];
} user_t;
```

```c
// backend/src/models/category.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  parent_id;
    char     name[128];
    char     type[32];
    char     asset_type[32];
    char     currency[16];
    char     icon[64];
    int      sort_order;
    char     created_at[64];
    char     updated_at[64];
} category_t;
```

```c
// backend/src/models/asset.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  category_id;
    char     name[128];
    char     account_no[64];
    double   current_value;
    char     currency[16];
    char     note[256];
    double   quantity;
    double   cost_basis;
    double   net_value;
    char     created_at[64];
    char     updated_at[64];
} asset_t;
```

```c
// backend/src/models/transaction.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  asset_id;
    int64_t  linked_asset_id;
    int64_t  category_id;
    char     transaction_type[32];
    char     source_type[32];
    int      direction;
    int      linked_direction;
    double   amount;
    double   price_per_unit;
    double   quantity;
    char     currency[16];
    char     transaction_date[32];
    char     note[256];
    char     created_at[64];
} transaction_t;
```

```c
// backend/src/models/daily_expense.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  category_id;
    int64_t  asset_id;
    char     expense_type[16];
    double   amount;
    char     currency[16];
    char     expense_date[32];
    char     note[256];
    char     created_at[64];
    char     updated_at[64];
} daily_expense_t;
```

```c
// backend/src/models/tag.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    char     name[128];
    char     color[16];
    char     created_at[64];
} tag_t;
```

```c
// backend/src/models/transfer.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  from_asset_id;
    int64_t  to_asset_id;
    double   amount;
    char     currency[16];
    char     transfer_date[32];
    char     note[256];
    char     created_at[64];
} transfer_t;
```

- [ ] **Step 2: 创建 dtos/request.h**

将 `swagger_types.h` 中所有请求体 struct 移入此文件：

```c
// backend/src/dtos/request.h
#pragma once
#include <stdint.h>

typedef struct { char username[128]; char password_enc[512]; char db_driver[64]; char db_dsn[512]; } setup_req_t;
typedef struct { char username[128]; char password[128]; } register_req_t;
typedef struct { char username[128]; char password_enc[512]; } login_req_t;
typedef struct { char old_password_enc[512]; char new_password_enc[512]; } change_pwd_req_t;
typedef struct { char name[128]; char type[32]; char asset_type[32]; char currency[16]; char icon[64]; int64_t parent_id; int sort_order; } category_req_t;
typedef struct { char name[128]; char account_no[64]; char currency[16]; char note[256]; int64_t category_id; double current_value; double quantity; double cost_basis; double net_value; } asset_req_t;
typedef struct { char currency[16]; char note[256]; char transaction_date[32]; int64_t asset_id; int64_t linked_asset_id; int64_t category_id; double amount; double price_per_unit; double quantity; double fee; char transaction_type[32]; char source_type[32]; } transaction_req_t;
typedef struct { char currency[16]; char expense_date[32]; char note[256]; int64_t category_id; int64_t asset_id; double amount; char expense_type[16]; } daily_expense_req_t;
typedef struct { char name[128]; char color[16]; } tag_req_t;
typedef struct { char currency[16]; char note[256]; char transfer_date[32]; int64_t from_asset_id; int64_t to_asset_id; double amount; } transfer_req_t;
```

- [ ] **Step 3: 创建 dtos/response.h**

```c
// backend/src/dtos/response.h
#pragma once
#include <stdint.h>

typedef struct { char token[512]; double expires_in; } token_resp_t;
typedef struct { int64_t id; char username[128]; char created_at[64]; } user_resp_t;
typedef struct { int64_t id; char name[128]; char parent_name[128]; char type[32]; char asset_type[32]; char currency[16]; char icon[64]; int64_t parent_id; int sort_order; } category_resp_t;
typedef struct { int64_t id; char name[128]; char account_no[64]; char currency[16]; char note[256]; char category_name[128]; char asset_type[32]; int64_t category_id; double current_value; double quantity; double cost_basis; double net_value; char created_at[64]; char updated_at[64]; } asset_resp_t;
typedef struct { int64_t id; char name[128]; char color[16]; char created_at[64]; } tag_resp_t;
typedef struct { int64_t id; int64_t asset_id; char transaction_type[32]; double amount; double quantity; double price_per_unit; char currency[16]; char transaction_date[32]; char note[256]; char created_at[64]; } transaction_resp_t;
```

- [ ] **Step 4: 创建空目录占位（后续 Task 填充内容）**

```bash
mkdir -p backend/src/{controllers,services,repositories,middlewares,config,models,dtos}
```

- [ ] **Step 5: Commit**

```bash
git add backend/src/models/ backend/src/dtos/ backend/src/controllers/ backend/src/services/ backend/src/repositories/ backend/src/middlewares/ backend/src/config/
git commit -m "chore(结构): 📦 create layered directory structure"
```

---

## Task 2: 中间件提取

**Files:**
- Create: `backend/src/middlewares/jwt_middleware.c`
- Create: `backend/src/middlewares/cors_middleware.c`
- Create: `backend/src/middlewares/csrf_middleware.c`
- Create: `backend/src/middlewares/jwt_middleware.h`
- Create: `backend/src/middlewares/cors_middleware.h`
- Create: `backend/src/middlewares/csrf_middleware.h`
- Modify: `backend/src/main.c`（移除中间件代码）

- [ ] **Step 1: 创建 middlewares/cors_middleware.h**

```c
// backend/src/middlewares/cors_middleware.h
#pragma once
#include "csilk/csilk.h"
void cors_middleware_wrapper(csilk_ctx_t* c);
```

- [ ] **Step 2: 创建 middlewares/cors_middleware.c**

从 `main.c` 复制 `cors_middleware_wrapper` 函数体：

```c
// backend/src/middlewares/cors_middleware.c
#include "middlewares/cors_middleware.h"

static void cors_middleware_wrapper(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    cors.allow_origin = "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}
```

- [ ] **Step 3: 创建 middlewares/jwt_middleware.h**

```c
// backend/src/middlewares/jwt_middleware.h
#pragma once
#include "csilk/csilk.h"
void jwt_middleware_wrapper(csilk_ctx_t* c);
```

- [ ] **Step 4: 创建 middlewares/jwt_middleware.c**

从 `main.c` 复制 `jwt_middleware_wrapper` 函数体（约 15 行）：

```c
// backend/src/middlewares/jwt_middleware.c
#include "middlewares/jwt_middleware.h"
#include <string.h>

static void jwt_middleware_wrapper(csilk_ctx_t* c) {
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
        return;
    }
    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret) secret = "minefolio-dev-secret-change-in-production";
    csilk_jwt_middleware(c, secret);
}
```

- [ ] **Step 5: 创建 middlewares/csrf_middleware.h**

```c
// backend/src/middlewares/csrf_middleware.h
#pragma once
#include "csilk/csilk.h"
void csrf_middleware_wrapper(csilk_ctx_t* c);
```

- [ ] **Step 6: 创建 middlewares/csrf_middleware.c**

从 `main.c` 复制 `csrf_middleware_wrapper` 函数体（约 35 行）。

- [ ] **Step 7: 从 main.c 删除三个中间件函数，改用 include**

在 `main.c` 顶部添加：
```c
#include "middlewares/jwt_middleware.h"
#include "middlewares/cors_middleware.h"
#include "middlewares/csrf_middleware.h"
```

删除 `jwt_middleware_wrapper`、`cors_middleware_wrapper`、`csrf_middleware_wrapper` 三个 static 函数（原行 62–139）。

- [ ] **Step 8: 编译验证**

```bash
cd backend && cmake --build build --parallel 2>&1 | tail -5
```
Expected: 编译成功，无错误。

- [ ] **Step 9: Commit**

```bash
git add backend/src/middlewares/ backend/src/main.c
git commit -m "refactor(middleware): ♻️ extract middleware from main.c into separate module"
```

---

## Task 3: config/ 模块 — RSA 密钥与 DB 初始化

**Files:**
- Create: `backend/src/config/key_manager.h`
- Create: `backend/src/config/key_manager.c`
- Create: `backend/src/config/db_config.h`
- Create: `backend/src/config/db_config.c`
- Modify: `backend/src/auth_key.c/h`（内容迁移到 key_manager）
- Modify: `backend/src/main.c`（移除 auth_key_init 调用）

- [ ] **Step 1: 创建 config/key_manager.h**

```c
// backend/src/config/key_manager.h
#pragma once
const char* auth_key_get_private_pem(void);
int         auth_key_init(void);
```

- [ ] **Step 2: 创建 config/key_manager.c**

将 `auth_key.c` 全部内容复制到 `key_manager.c`，将 `#include "auth_key.h"` 改为 `#include "config/key_manager.h"`。

- [ ] **Step 3: 创建 config/db_config.h**

```c
// backend/src/config/db_config.h
#pragma once
#include "csilk/drivers/db.h"
int  db_config_init(csilk_db_pool_t** out_pool);
int  db_config_run_migrations(csilk_db_pool_t* pool);
```

- [ ] **Step 4: 创建 config/db_config.c**

从 `main.c` 提取 `db_init` 和 `db_run_migrations` 调用逻辑：

```c
// backend/src/config/db_config.c
#include "config/db_config.h"
#include "common/db.h"
#include <stdio.h>

int db_config_init(csilk_db_pool_t** out_pool) {
    return db_init(out_pool);
}

int db_config_run_migrations(csilk_db_pool_t* pool) {
    return db_run_migrations(pool);
}
```

- [ ] **Step 5: 更新 main.c**

保留 `#include "config/key_manager.h"` 和 `#include "config/db_config.h"`，将原来的 `auth_key_init()` 调用替换为 `key_manager_init()`（如需改名），将 `db_init(&pool)` 替换为 `db_config_init(&pool)`。

- [ ] **Step 6: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

- [ ] **Step 7: Commit**

```bash
git add backend/src/config/ backend/src/main.c
git commit -m "refactor(config): ♻️ extract key manager and db config into config/ module"
```

---

## Task 4: repositories/user_repo

**Files:**
- Create: `backend/src/repositories/user_repo.h`
- Create: `backend/src/repositories/user_repo.c`
- Modify: `backend/src/services/auth_service.c`（后续 Task 创建）

- [ ] **Step 1: 创建 repositories/user_repo.h**

```c
// backend/src/repositories/user_repo.h
#pragma once
#include "csilk/drivers/db.h"
#include "models/user.h"
int user_exists_by_username(csilk_db_pool_t* pool, const char* username);
int user_find_by_username(csilk_db_pool_t* pool, const char* username, user_t* out);
int user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash, int64_t* out_id);
int user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* new_hash);
int user_find_by_id(csilk_db_pool_t* pool, int64_t user_id, user_t* out);
```

- [ ] **Step 2: 创建 repositories/user_repo.c**

从 `auth.c` 提取所有 user 相关的 SQL 查询，改为接受/返回 `user_t` 结构体：

```c
// backend/src/repositories/user_repo.c
#include "repositories/user_repo.h"
#include "common/db.h"
#include <stdio.h>

int user_exists_by_username(csilk_db_pool_t* pool, const char* username) {
    const char* params[] = { username, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "SELECT COUNT(*) as cnt FROM users WHERE username = ?", params);
    int exists = 0;
    if (res && csilk_json_array_size(res) > 0)
        exists = db_get_int(csilk_json_array_get(res, 0), "cnt") > 0;
    if (res) csilk_json_free(res);
    return exists;
}

int user_find_by_username(csilk_db_pool_t* pool, const char* username, user_t* out) {
    const char* params[] = { username, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "SELECT id, username, password FROM users WHERE username = ?", params);
    int found = 0;
    if (res && csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        out->id       = db_get_int(row, "id");
        const char* un = csilk_json_get_string(row, "username");
        if (un) strncpy(out->username, un, sizeof(out->username) - 1);
        const char* pw = csilk_json_get_string(row, "password");
        if (pw) strncpy(out->password, pw, sizeof(out->password) - 1);
        found = 1;
    }
    if (res) csilk_json_free(res);
    return found;
}

int user_insert(csilk_db_pool_t* pool, const char* username, const char* password_hash, int64_t* out_id) {
    const char* params[] = { username, password_hash, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "INSERT INTO users (username, password) VALUES (?, ?) RETURNING id", params);
    int ok = 0;
    if (res && csilk_json_array_size(res) > 0) {
        *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
        ok = 1;
    }
    if (res) csilk_json_free(res);
    return ok;
}

int user_update_password(csilk_db_pool_t* pool, int64_t user_id, const char* new_hash) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char* params[] = { new_hash, uid_str, NULL };
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "UPDATE users SET password = ? WHERE id = ?", params);
    int ok = res != NULL;
    if (res) csilk_json_free(res);
    return ok;
}

int user_find_by_id(csilk_db_pool_t* pool, int64_t user_id, user_t* out) {
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_json_t* res = csilk_db_query_param_json(pool,
        "SELECT id, username, created_at FROM users WHERE id = ?",
        (const char*[]){uid_str, NULL});
    int found = 0;
    if (res && csilk_json_array_size(res) > 0) {
        csilk_json_t* row = csilk_json_array_get(res, 0);
        out->id       = db_get_int(row, "id");
        const char* un = csilk_json_get_string(row, "username");
        if (un) strncpy(out->username, un, sizeof(out->username) - 1);
        const char* ca = csilk_json_get_string(row, "created_at");
        if (ca) strncpy(out->created_at, ca, sizeof(out->created_at) - 1);
        found = 1;
    }
    if (res) csilk_json_free(res);
    return found;
}
```

- [ ] **Step 3: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add backend/src/repositories/user_repo.*
git commit -m "feat(repo): ✨ add user repository with CRUD queries"
```

---

## Task 5: repositories/category_repo

**Files:**
- Create: `backend/src/repositories/category_repo.h`
- Create: `backend/src/repositories/category_repo.c`

- [ ] **Step 1: 创建 repositories/category_repo.h**

```c
// backend/src/repositories/category_repo.h
#pragma once
#include "csilk/drivers/db.h"
#include "models/category.h"
int   category_insert(csilk_db_pool_t* pool, int64_t user_id, const category_t* cat);
int   category_update(csilk_db_pool_t* pool, const category_t* cat);
int   category_delete(csilk_db_pool_t* pool, int64_t id, int64_t user_id);
int   category_count_by_user(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* category_list_by_user(csilk_db_pool_t* pool, int64_t user_id);
csilk_json_t* category_children_by_id(csilk_db_pool_t* pool, int64_t id, int64_t user_id);
```

- [ ] **Step 2: 创建 repositories/category_repo.c**

从 `categories.c` 提取所有 SQL 查询为独立函数，使用 `category_t` 作为参数/返回值。核心查询包括：INSERT、UPDATE、DELETE、SELECT（列表/子节点）。

- [ ] **Step 3: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add backend/src/repositories/category_repo.*
git commit -m "feat(repo): ✨ add category repository"
```

---

## Task 6: repositories/asset_repo

**Files:**
- Create: `backend/src/repositories/asset_repo.h`
- Create: `backend/src/repositories/asset_repo.c`

从 `assets.c` 和 `asset_logs.c` 提取所有 SQL 查询。需支持的函数：
- `asset_insert(pool, asset, user_id)` 
- `asset_update(pool, asset)`
- `asset_delete(pool, id, user_id)`
- `asset_find_by_id(pool, id, user_id, asset*)`
- `asset_list_by_user(pool, user_id, cat_id, page, page_size, total*)`
- `asset_balance_logs_list(pool, user_id, asset_id, page, page_size, total*)`
- `asset_transaction_history(pool, asset_id, user_id)` — 用于 detail 接口

- [ ] **Step 1: 编写 asset_repo.h 和 asset_repo.c**（参照 Task 5 模式）

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add backend/src/repositories/asset_repo.*
git commit -m "feat(repo): ✨ add asset repository"
```

---

## Task 7: repositories/transaction_repo

**Files:**
- Create: `backend/src/repositories/transaction_repo.h`
- Create: `backend/src/repositories/transaction_repo.c`

从 `transactions.c` 提取所有 SQL 查询：
- `transaction_list(pool, user_id, filters, page, page_size, total*)`
- `transaction_monthly_summary(pool, user_id, year, month)`
- `transaction_insert(pool, tx, user_id, tx_id_out)`
- `transaction_update(pool, tx)`
- `transaction_delete(pool, id, user_id)`
- `transaction_export_csv(pool, user_id)` → 返回 json array

- [ ] **Step 1: 编写 transaction_repo.h 和 transaction_repo.c**

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add backend/src/repositories/transaction_repo.*
git commit -m "feat(repo): ✨ add transaction repository"
```

---

## Task 8: repositories/daily_expense_repo

**Files:**
- Create: `backend/src/repositories/daily_expense_repo.h`
- Create: `backend/src/repositories/daily_expense_repo.c`

从 `daily_expenses.c` 提取：
- `daily_expense_insert(pool, exp, user_id)`
- `daily_expense_update(pool, exp)`
- `daily_expense_delete(pool, id, user_id)`
- `daily_expense_list(pool, user_id, filters, page, page_size, total*)`
- `daily_expense_monthly_summary(pool, user_id, year, month)`
- `tag_find_or_create(pool, user_id, tag_name, color)` 

- [ ] **Step 1: 编写文件**

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/repositories/
git commit -m "feat(repo): ✨ add daily_expense and tag repositories"
```

---

## Task 9: repositories/tag_repo + transfer_repo

**Files:**
- Create: `backend/src/repositories/tag_repo.h/.c`
- Create: `backend/src/repositories/transfer_repo.h/.c`

从 `tags.c` 和 `transfers.c` 提取查询逻辑。

- [ ] **Step 1: 编写 tag_repo（list, insert, update, delete）**

- [ ] **Step 2: 编写 transfer_repo（insert with BEGIN/COMMIT）**

- [ ] **Step 3: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/repositories/
git commit -m "feat(repo): ✨ add tag and transfer repositories"
```

---

## Task 10: services/auth_service

**Files:**
- Create: `backend/src/services/auth_service.h`
- Create: `backend/src/services/auth_service.c`

服务层函数：
- `auth_service_seed_defaults(pool, user_id)` — 从 categories.c 提取
- `auth_service_change_password(pool, user_id, old_hash, new_hash)` — 从 auth.c 提取
- `auth_service_get_user(pool, user_id, user_t*)` — 调用 user_repo

- [ ] **Step 1: 编写 auth_service.h/.c**

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add backend/src/services/auth_service.*
git commit -m "feat(service): ✨ add auth service layer"
```

---

## Task 11: services/category_service

**Files:**
- Create: `backend/src/services/category_service.h`
- Create: `backend/src/services/category_service.c`

服务层函数：
- `category_service_build_tree(pool, user_id)` — 从 categories.c `build_tree()` 提取
- `category_service_find_or_create(pool, user_id, name, parent_id, type, asset_type, icon, sort_order, id_out)`
- `category_service_seed_defaults(pool, user_id)`

- [ ] **Step 1: 编写 category_service.h/.c**

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/services/category_service.*
git commit -m "feat(service): ✨ add category service"
```

---

## Task 12: services/asset_service

**Files:**
- Create: `backend/src/services/asset_service.h`
- Create: `backend/src/services/asset_service.c`

服务层函数：
- `asset_service_create(pool, user_id, req, asset_id_out)` — 含投资类自动市值推导
- `asset_service_update(pool, user_id, req, asset_id)` — 含净值重算+余额联动
- `asset_service_delete(pool, user_id, asset_id)`
- `asset_service_get_detail(pool, user_id, asset_id, asset_resp_t*)` — 含关联交易历史

- [ ] **Step 1: 编写 asset_service.h/.c**

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/services/asset_service.*
git commit -m "feat(service): ✨ add asset service"
```

---

## Task 13: services/transaction_service

**Files:**
- Create: `backend/src/services/transaction_service.h`
- Create: `backend/src/services/transaction_service.c`

服务层函数：
- `transaction_service_create(pool, user_id, req, tx_id_out)` — 含持仓追踪+fee行插入
- `transaction_service_update(pool, user_id, req, tx_id)`
- `transaction_service_delete(pool, user_id, tx_id)`
- `transaction_service_monthly_summary(pool, user_id, year, month, resp*)`

- [ ] **Step 1: 编写 transaction_service.h/.c**

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/services/transaction_service.*
git commit -m "feat(service): ✨ add transaction service"
```

---

## Task 14: services/daily_expense_service + tag_service

**Files:**
- Create: `backend/src/services/daily_expense_service.h/.c`
- Create: `backend/src/services/tag_service.h/.c`

- [ ] **Step 1: 编写两个 service 文件**

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/services/
git commit -m "feat(service): ✨ add daily_expense and tag services"
```

---

## Task 15: services/transfer_service + report_service

**Files:**
- Create: `backend/src/services/transfer_service.h/.c`
- Create: `backend/src/services/report_service.h/.c`

transfer_service：转账事务（BEGIN→INSERT transfers+transactions→balance_apply_delta→COMMIT/ROLLBACK）
report_service：所有报告查询逻辑（expense monthly/trend/yearly/category/tag，asset trend/breakdown/performance/holdings/summary）

- [ ] **Step 1: 编写 transfer_service**

- [ ] **Step 2: 编写 report_service**（从 reports.c 796 行中提取所有业务逻辑）

- [ ] **Step 3: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/services/
git commit -m "feat(service): ✨ add transfer and report services"
```

---

## Task 16: controllers/auth_controller

**Files:**
- Create: `backend/src/controllers/auth_controller.h`
- Create: `backend/src/controllers/auth_controller.c`

薄层 controller，调用 auth_service：
- `system_status(c)` — 简单查询，可直接调 user_repo
- `system_setup(c)` — 调用 auth_service_seed_defaults
- `auth_register(c)` — 调用 user_repo + jwt_generate_token
- `auth_login(c)` — 调用 user_repo + bcrypt + jwt_generate_token
- `auth_public_key(c)` — 调用 key_manager
- `auth_me(c)` — 调用 user_repo + respond_ok
- `auth_change_password(c)` — 调用 auth_service

- [ ] **Step 1: 编写 auth_controller.c**

```c
// backend/src/controllers/auth_controller.c
#include "controllers/auth_controller.h"
#include "services/auth_service.h"
#include "repositories/user_repo.h"
#include "config/key_manager.h"
#include "common/response.h"
#include "common/jwt.h"
#include "dtos/response.h"
#include "dtos/request.h"
#include "csilk/csilk.h"
#include <string.h>

void system_status(csilk_ctx_t* c) {
    csilk_db_pool_t* pool = db_get_pool();
    int count = 0;
    csilk_json_t* res = csilk_db_query_json(pool, "SELECT COUNT(*) as count FROM users");
    if (res && csilk_json_array_size(res) > 0)
        count = (int)db_get_int(csilk_json_array_get(res, 0), "count");
    if (res) csilk_json_free(res);
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_bool(resp, "initialized", count > 0);
    csilk_json_add_number(resp, "user_count", count);
    respond_ok(c, resp);
}

// ... 其余 handler 类似，调用 service 后 respond_ok/respond_error
```

- [ ] **Step 2: 编译验证**

```bash
cmake --build build --parallel 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add backend/src/controllers/auth_controller.*
git commit -m "feat(controller): ✨ add auth controller"
```

---

## Task 17: controllers/category_controller + tag_controller

**Files:**
- Create: `backend/src/controllers/category_controller.h/.c`
- Create: `backend/src/controllers/tag_controller.h/.c`

- [ ] **Step 1: 编写两个 controller 文件**
  - category: list, create, update, delete, children（调用 category_service）
  - tag: list, create, update, delete, suggestions（直接调 tag_repo）

- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/controllers/
git commit -m "feat(controller): ✨ add category and tag controllers"
```

---

## Task 18: controllers/asset_controller + transfer_controller

**Files:**
- Create: `backend/src/controllers/asset_controller.h/.c`
- Create: `backend/src/controllers/transfer_controller.h/.c`

- [ ] **Step 1: 编写 asset_controller**（list/create/update/delete/detail，调 asset_service）
- [ ] **Step 2: 编写 transfer_controller**（create，调 transfer_service）
- [ ] **Step 3: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/controllers/
git commit -m "feat(controller): ✨ add asset and transfer controllers"
```

---

## Task 19: controllers/transaction_controller + daily_expense_controller

**Files:**
- Create: `backend/src/controllers/transaction_controller.h/.c`
- Create: `backend/src/controllers/daily_expense_controller.h/.c`

- [ ] **Step 1: 编写两个 controller 文件**
- [ ] **Step 2: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/controllers/
git commit -m "feat(controller): ✨ add transaction and daily_expense controllers"
```

---

## Task 20: controllers/report_controller + import_export_controller

**Files:**
- Create: `backend/src/controllers/report_controller.h/.c`
- Create: `backend/src/controllers/import_export_controller.h/.c`

report_controller：10 个报告 handler，全部调 report_service
import_export_controller：4 个 CSV handler，调 import/export 逻辑

- [ ] **Step 1: 编写 report_controller.c**（调用 report_service 的各函数）
- [ ] **Step 2: 编写 import_export_controller.c**（CSV 解析+IO）
- [ ] **Step 3: 编译验证 + Commit**

```bash
cmake --build build --parallel 2>&1 | tail -5
git add backend/src/controllers/
git commit -m "feat(controller): ✨ add report and import_export controllers"
```

---

## Task 21: 精简 main.c 并完成最终迁移

**Files:**
- Modify: `backend/src/main.c`
- Delete: `backend/src/auth.c`, `auth_key.c`, `auth_key.h`, `assets.c`, `asset_logs.c`, `categories.c`, `categories.h`, `daily_expenses.c`, `tags.c`, `transactions.c`, `transfers.c`, `reports.c`, `import_export.c`

- [ ] **Step 1: 重写 main.c（~60 行）**

```c
// backend/src/main.c
#include "csilk/app/app.h"
#include "swagger_types.h"
#include "config/db_config.h"
#include "config/key_manager.h"
#include "middlewares/jwt_middleware.h"
#include "middlewares/cors_middleware.h"
#include "middlewares/csrf_middleware.h"
#include "controllers/auth_controller.h"
#include "controllers/category_controller.h"
#include "controllers/asset_controller.h"
#include "controllers/transaction_controller.h"
#include "controllers/daily_expense_controller.h"
#include "controllers/tag_controller.h"
#include "controllers/transfer_controller.h"
#include "controllers/report_controller.h"
#include "controllers/import_export_controller.h"
#include <stdio.h>

// Forward declarations: 9 entry points (one per domain)
extern void controller_auth_register(csilk_ctx_t* c);
extern void controller_auth_login(csilk_ctx_t* c);
// ... 9 个 controller 入口函数
```

实际路由注册：
```c
    // System (public)
    csilk_app_get_ext(app, "/api/system/status", system_status, nullptr, nullptr, "System status", "...");
    csilk_app_post_ext(app, "/api/system/setup", system_setup, "minefolio_setup_req_t", "minefolio_token_resp_t", "...");
    csilk_app_post_ext(app, "/api/auth/register", auth_register, "minefolio_register_req_t", "minefolio_token_resp_t", "...");
    csilk_app_post_ext(app, "/api/auth/login", auth_login, "minefolio_login_req_t", "minefolio_token_resp_t", "...");
    csilk_app_get_ext(app, "/api/auth/public-key", auth_public_key, nullptr, nullptr, "Get public key", "...");

    // JWT group
    csilk_app_use_group(app, "/api", jwt_middleware_wrapper);
    if (getenv("MINEFOLIO_ENABLE_CSRF"))
        csilk_app_use_group(app, "/api", csrf_middleware_wrapper);

    // Auth
    csilk_app_get_ext(app, "/api/auth/me", auth_me, nullptr, "minefolio_user_resp_t", "...");
    csilk_app_put_ext(app, "/api/auth/password", auth_change_password, "minefolio_change_pwd_req_t", nullptr, "...");

    // Categories
    csilk_app_get_ext(app, "/api/categories", categories_list, nullptr, "minefolio_category_resp_t", "...");
    csilk_app_post_ext(app, "/api/categories", categories_create, "minefolio_category_req_t", "minefolio_category_resp_t", "...");
    csilk_app_put_ext(app, "/api/categories/:id", categories_update, "minefolio_category_req_t", "minefolio_category_resp_t", "...");
    csilk_app_delete_ext(app, "/api/categories/:id", categories_delete, nullptr, nullptr, "...");
    csilk_app_get_ext(app, "/api/categories/:id/children", categories_children, nullptr, "minefolio_category_resp_t", "...");

    // Assets
    // Transactions
    // Daily expenses
    // Tags
    // Transfers
    // Reports
    // Summary
    // Asset balance logs

    csilk_app_static(app, "/", "./frontend/dist");
    printf("Starting Minefolio server on :8080\n");
    csilk_app_run(app, 8080);
```

- [ ] **Step 2: 删除旧文件**

```bash
rm backend/src/auth.c backend/src/auth_key.c backend/src/auth_key.h
rm backend/src/assets.c backend/src/asset_logs.c
rm backend/src/categories.c backend/src/categories.h
rm backend/src/daily_expenses.c
rm backend/src/tags.c
rm backend/src/transactions.c
rm backend/src/transfers.c
rm backend/src/reports.c
rm backend/src/import_export.c
rm backend/src/swagger_types.h   # DTO 已移至 dtos/
```

- [ ] **Step 3: 编译 + 全量测试**

```bash
cmake --build build --parallel 2>&1
bash tests/test_link.sh
```

Expected: PASS=103 FAIL=0

- [ ] **Step 4: 验证 OpenAPI 和 Swagger UI**

```bash
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/minefolio_test.db ./build/minefolio &
sleep 2
curl -s http://localhost:8080/openapi.json | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'paths: {len(d[\"paths\"])}')"
curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/docs
kill %1; wait; rm -f /tmp/minefolio_test.db
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(structure): ♻️ complete MVC refactor — controllers/services/repositories"
```

---

## Task 22: 清理与验证

- [ ] **Step 1: 最终全量验证**

```bash
cmake --build build --parallel && npm --prefix frontend run build && bash tests/test_link.sh
```

Expected: 后端编译通过，前端构建干净，103 项测试全部 PASS。

- [ ] **Step 2: 确认 Swagger UI 正常**

```bash
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/minefolio_final.db ./build/minefolio &
sleep 2
echo "=== schemas ===" && curl -s http://localhost:8080/openapi.json | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'schemas: {len(d[\"components\"][\"schemas\"])}')"
echo "=== docs ===" && curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/docs
kill %1; wait; rm -f /tmp/minefolio_final.db
```

- [ ] **Step 3: 删除临时分支**

```bash
git branch -D refactor/before-structure
```

- [ ] **Step 4: Final commit message**

```
refactor(structure): ♻️ reorganize backend into controllers/services/repositories layers
```

---

## 实施顺序总结

```
Task 1  →  创建目录 + models + dtos
Task 2  →  中间件提取
Task 3  →  config 模块
Task 4  →  user_repo
Task 5  →  category_repo
Task 6  →  asset_repo
Task 7  →  transaction_repo
Task 8  →  daily_expense_repo + tag_repo
Task 9  →  transfer_repo
Task 10 →  auth_service
Task 11 →  category_service
Task 12 →  asset_service
Task 13 →  transaction_service
Task 14 →  daily_expense_service + tag_service
Task 15 →  transfer_service + report_service
Task 16 →  auth_controller
Task 17 →  category_controller + tag_controller
Task 18 →  asset_controller + transfer_controller
Task 19 →  transaction_controller + daily_expense_controller
Task 20 →  report_controller + import_export_controller
Task 21 → 精简 main.c + 删除旧文件
Task 22 → 全量验证
```

每个 Task 结束后均运行 `cmake --build build --parallel && bash tests/test_link.sh` 确保不破坏现有功能。
