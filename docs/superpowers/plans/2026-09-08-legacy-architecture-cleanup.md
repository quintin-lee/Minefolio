# Minefolio 新旧双轨架构清理计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完全移除 Legacy Controllers/Services 层，所有路由统一走 DDD 四层架构（Interfaces → Application → Domain → Infrastructure），消除双轨并存的技术债。

**Architecture:** 保持现有 DDD 四层架构不变，将所有"半迁移"控制器中的 Legacy Service/Repository 直接调用替换为 Application Use Case 调用，将"未迁移"控制器中的业务逻辑下沉到 Application/Domain 层，最终删除旧 `controllers/`、`services/`、`repositories/` 中已废弃的文件。

**Tech Stack:** C23, csilk v0.5.2, DDD Four-Layer Architecture

---

## 迁移状态总览

| 域 | Interfaces | Application | Domain | Infrastructure | 状态 |
|----|:----------:|:-----------:|:------:|:--------------:|------|
| auth | ✅ | ✅ | ✅ | ✅ | **已完成** |
| asset | ✅ | ✅ | ✅ | ✅ | **已完成** |
| transaction | ✅ | ✅ | ✅ | ✅ | **已完成** |
| cashflow | ✅ | ✅ | ✅ | ✅ | **已完成** |
| daily_expense | ⚠️ 调用 legacy service | — | — | — | **半迁移** |
| report | ⚠️ 调用 legacy service | — | — | — | **半迁移** |
| transfer | ⚠️ controller 内含业务逻辑 | — | — | — | **半迁移** |
| dca | ⚠️ controller 内含业务逻辑 | — | — | — | **半迁移** |
| market | ✅ | ✅ | ✅ | ✅ | **已完成** |
| category | — | — | — | — | **未迁移** |
| tag | — | — | — | — | **未迁移** |
| ledger | ⚠️ 直接调用 legacy repo | — | — | — | **未迁移** |
| file | — | — | — | — | **未迁移** |
| import/export | — | — | — | — | **未迁移** |
| import_rule | — | — | — | — | **未迁移** |
| receipt | — | — | — | — | **未迁移** |
| admin | — | — | — | — | **未迁移** |

---

## 文件结构

### 已完成域（不动）
```
backend/src/
├── interfaces/http/controllers/
│   ├── auth_controller.c         # ✅ 已委托 application/auth
│   ├── asset_controller.c        # ✅ 已委托 application/asset
│   ├── transaction_controller.c  # ✅ 已委托 application/transaction
│   ├── cashflow_controller.c     # ✅ 已委托 application/cashflow
│   └── market_controller.c       # ✅ 已委托 application/market
├── application/{domain}/usecases.h  # ✅ 已有
├── domain/{entity,rules,repository}.h  # ✅ 已有
└── infrastructure/repositories/*_repo_impl.h  # ✅ 已有
```

### 待迁移域（目标结构）
```
backend/src/
├── interfaces/http/controllers/
│   ├── daily_expense_controller.c  # 🔄 改为调用 application/daily_expense
│   ├── report_controller.c         # 🔄 改为调用 application/report
│   ├── transfer_controller.c       # 🔄 改为调用 application/transfer
│   ├── dca_controller.c            # 🔄 改为调用 application/dca
│   ├── category_controller.c       # 🔄 改为调用 application/category
│   ├── tag_controller.c            # 🔄 改为调用 application/tag
│   ├── ledger_controller.c         # 🔄 改为调用 application/ledger
│   └── ...其余
├── application/
│   ├── daily_expense/              # 🆕 创建
│   │   ├── commands.h
│   │   ├── dtos.h
│   │   ├── usecases.h
│   │   └── usecases.c
│   ├── report/                     # 🆕 创建
│   ├── transfer/                   # 🆕 创建
│   ├── dca/                        # 🆕 创建
│   ├── category/                   # 🆕 创建
│   ├── tag/                        # 🆕 创建
│   └── ledger/                     # 🆕 创建
├── domain/
│   ├── daily_expense/              # 🆕 创建 (entity + rules + repository)
│   ├── report/                     # 🆕 创建 (仅 rules, 无 repo)
│   ├── transfer/                   # 🆕 创建
│   ├── dca/                        # 🆕 创建
│   ├── category/                   # 🆕 创建
│   ├── tag/                        # 🆕 创建
│   └── ledger/                     # 🆕 创建
└── infrastructure/repositories/
    ├── daily_expense_repo_impl.*   # 🆕 创建
    ├── transfer_repo_impl.*        # 🆕 创建
    ├── dca_repo_impl.*             # 🆕 创建
    ├── category_repo_impl.*        # 🆕 创建
    ├── tag_repo_impl.*             # 🆕 创建
    └── ledger_repo_impl.*          # 🆕 创建
```

### 待删除的废弃文件（迁移完成后）
```
backend/src/controllers/            # 🗑️ 旧 controllers（已被 interfaces/ 取代）
backend/src/services/
├── daily_expense_query.c           # 🗑️ 被 application/daily_expense 取代
├── daily_expense_write.c           # 🗑️
├── report_expense_service.c        # 🗑️ 被 application/report 取代
├── report_asset_service.c          # 🗑️
├── report_holdings_service.c       # 🗑️
├── category_service.c              # 🗑️ 被 application/category 取代
├── export_service.c                # 🗑️ 被 application/import_export 取代
├── import_service.c                # 🗑️
├── file_parser.c                   # 🗑️
└── ai_service.c                    # ⚠️ 保留（AI Runtime 已在 services/ai/ 中）
backend/src/repositories/
├── daily_expense_repo.c            # 🗑️ 被 infrastructure/repositories 取代
├── transfer_repo.c                 # 🗑️
├── dca_repo.c                      # 🗑️
├── category_repo.c                 # 🗑️
├── tag_repo.c                      # 🗑️
├── ledger_repo.c                   # 🗑️
└── import_rule_repo.c              # 🗑️
```

---

## 迁移策略

### 核心原则

1. **逐域迁移，每域独立可测**：每次只迁移一个域，完成后跑全量测试确认无回归
2. **Application Use Case 委托 Domain Rules + Infrastructure Repo**：新 usecase 不包含 SQL，只编排规则与仓储
3. **Controller 只做参数提取 + 响应封装**：Controller 不包含业务逻辑
4. **Domain 层零外部依赖**：Domain entity/rules 只引用 `core/financial/`，不引用 csilk/db/json
5. **保持 API 兼容**：前端零改动，所有 HTTP 端点路径、参数、响应格式不变

### 迁移顺序（按风险从低到高）

```
Phase 1: 简单 CRUD 域（低风险）
  ├── tag（最简单，纯 CRUD）
  ├── category（树形结构 CRUD）
  └── ledger（RBAC + CRUD）

Phase 2: 业务编排域（中等风险）
  ├── daily_expense（已有 legacy service，替换调用链）
  ├── transfer（controller 含业务逻辑，需下沉）
  └── dca（controller 含业务逻辑，需下沉）

Phase 3: 报表查询域（中等风险）
  └── report（纯查询，无写入，风险低但代码量大）

Phase 4: 辅助域（低风险）
  ├── file（文件上传/解析）
  ├── import/export（CSV 导入导出）
  ├── import_rule（智能分类规则）
  └── receipt（OCR 识别）

Phase 5: 清理
  ├── 删除 controllers/ 目录
  ├── 删除 services/ 中已废弃文件
  ├── 删除 repositories/ 中已废弃文件
  ├── 更新 CMakeLists.txt
  └── 更新 AGENTS.md 架构文档
```

---

## Phase 1: 简单 CRUD 域

### Task 1: Tag 域迁移

**Files:**
- Create: `backend/src/domain/tag/entity.h`
- Create: `backend/src/domain/tag/repository.h`
- Create: `backend/src/domain/tag/rules.h`
- Create: `backend/src/domain/tag/rules.c`
- Create: `backend/src/application/tag/commands.h`
- Create: `backend/src/application/tag/dtos.h`
- Create: `backend/src/application/tag/usecases.h`
- Create: `backend/src/application/tag/usecases.c`
- Create: `backend/src/infrastructure/repositories/tag_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/tag_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/tag_controller.c`

- [ ] **Step 1: 创建 Domain Entity**

```c
// backend/src/domain/tag/entity.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    char     name[128];
    char     color[32];
    char     created_at[32];
    char     updated_at[32];
} mf_tag_t;
```

- [ ] **Step 2: 创建 Domain Repository Contract**

```c
// backend/src/domain/tag/repository.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "domain/tag/entity.h"

int mf_tag_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_tag_t* out);
int mf_tag_repo_list(void* db_pool, int64_t user_id, mf_tag_t** out_list, size_t* out_count);
int mf_tag_repo_create(void* db_pool, int64_t user_id, const char* name, const char* color, int64_t* out_id);
int mf_tag_repo_update(void* db_pool, int64_t user_id, int64_t id, const char* name, const char* color);
int mf_tag_repo_delete(void* db_pool, int64_t user_id, int64_t id);
void mf_tag_repo_free_list(mf_tag_t* list, size_t count);
```

- [ ] **Step 3: 创建 Domain Rules**

```c
// backend/src/domain/tag/rules.h
#pragma once
#include <stdbool.h>

bool mf_tag_rule_validate_name(const char* name);
bool mf_tag_rule_validate_color(const char* color);
```

```c
// backend/src/domain/tag/rules.c
#include "domain/tag/rules.h"
#include <string.h>

bool
mf_tag_rule_validate_name(const char* name)
{
    return name && name[0] && strlen(name) <= 128;
}

bool
mf_tag_rule_validate_color(const char* color)
{
    if (!color || !color[0]) return true; // optional
    return color[0] == '#' && strlen(color) == 7;
}
```

- [ ] **Step 4: 创建 Application Commands & DTOs**

```c
// backend/src/application/tag/commands.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t     user_id;
    const char* name;
    const char* color;
} create_tag_cmd_t;

typedef struct {
    int64_t     user_id;
    int64_t     tag_id;
    const char* name;
    const char* color;
} update_tag_cmd_t;

typedef struct {
    int64_t user_id;
    int64_t tag_id;
} delete_tag_cmd_t;
```

```c
// backend/src/application/tag/dtos.h
#pragma once
#include <stdint.h>

typedef struct {
    int    code;
    char   message[256];
} tag_usecase_result_t;
```

- [ ] **Step 5: 创建 Application Use Cases**

```c
// backend/src/application/tag/usecases.h
#pragma once
#include "csilk/csilk.h"
#include "application/tag/commands.h"
#include "application/tag/dtos.h"

int tag_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list);
int tag_usecase_create(void* pool, const create_tag_cmd_t* cmd, int64_t* out_id, tag_usecase_result_t* out_res);
int tag_usecase_update(void* pool, const update_tag_cmd_t* cmd, tag_usecase_result_t* out_res);
int tag_usecase_delete(void* pool, const delete_tag_cmd_t* cmd, tag_usecase_result_t* out_res);
```

```c
// backend/src/application/tag/usecases.c
#include "application/tag/usecases.h"
#include "domain/tag/rules.h"
#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/db.h"
#include <string.h>

int
tag_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list)
{
    mf_tag_t* tags = NULL;
    size_t    count = 0;
    if (mf_tag_repo_list(pool, user_id, &tags, &count) != 0) {
        return -1;
    }
    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        csilk_json_t* obj = csilk_json_object();
        csilk_json_add_number(obj, "id", (double)tags[i].id);
        csilk_json_add_string(obj, "name", tags[i].name);
        csilk_json_add_string(obj, "color", tags[i].color);
        csilk_json_add_string(obj, "created_at", tags[i].created_at);
        csilk_json_array_add(arr, obj);
    }
    mf_tag_repo_free_list(tags, count);
    *out_list = arr;
    return 0;
}

int
tag_usecase_create(void* pool, const create_tag_cmd_t* cmd, int64_t* out_id, tag_usecase_result_t* out_res)
{
    if (!mf_tag_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "标签名称无效");
        return -1;
    }
    if (!mf_tag_rule_validate_color(cmd->color)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "颜色格式无效");
        return -1;
    }
    return mf_tag_repo_create(pool, cmd->user_id, cmd->name, cmd->color, out_id);
}

int
tag_usecase_update(void* pool, const update_tag_cmd_t* cmd, tag_usecase_result_t* out_res)
{
    if (!mf_tag_rule_validate_name(cmd->name)) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "标签名称无效");
        return -1;
    }
    return mf_tag_repo_update(pool, cmd->user_id, cmd->tag_id, cmd->name, cmd->color);
}

int
tag_usecase_delete(void* pool, const delete_tag_cmd_t* cmd, tag_usecase_result_t* out_res)
{
    return mf_tag_repo_delete(pool, cmd->user_id, cmd->tag_id);
}
```

- [ ] **Step 6: 创建 Infrastructure Repository Implementation**

```c
// backend/src/infrastructure/repositories/tag_repo_impl.c
#include "infrastructure/repositories/tag_repo_impl.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>

int
mf_tag_repo_list(void* db_pool, int64_t user_id, mf_tag_t** out_list, size_t* out_count)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* rows = csilk_db_query_param_json(
        pool,
        "SELECT id, user_id, name, color, created_at, updated_at FROM tags WHERE user_id=? ORDER BY id",
        (const char*[]){uid_str, NULL});

    if (!rows) {
        *out_list = NULL;
        *out_count = 0;
        return 0;
    }

    size_t n = csilk_json_array_size(rows);
    mf_tag_t* list = n > 0 ? malloc(sizeof(mf_tag_t) * n) : NULL;
    for (size_t i = 0; i < n; i++) {
        csilk_json_t* r = csilk_json_array_get(rows, i);
        list[i].id = db_get_int(r, "id");
        list[i].user_id = db_get_int(r, "user_id");
        strncpy(list[i].name, csilk_json_get_string(r, "name") ?: "", sizeof(list[i].name) - 1);
        strncpy(list[i].color, csilk_json_get_string(r, "color") ?: "", sizeof(list[i].color) - 1);
        strncpy(list[i].created_at, csilk_json_get_string(r, "created_at") ?: "", sizeof(list[i].created_at) - 1);
        strncpy(list[i].updated_at, csilk_json_get_string(r, "updated_at") ?: "", sizeof(list[i].updated_at) - 1);
    }
    csilk_json_free(rows);
    *out_list = list;
    *out_count = n;
    return 0;
}

int
mf_tag_repo_create(void* db_pool, int64_t user_id, const char* name, const char* color, int64_t* out_id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO tags (user_id, name, color) VALUES (?, ?, ?) RETURNING id",
        (const char*[]){uid_str, name, color && color[0] ? color : "#409EFF", NULL});

    if (!res || csilk_json_array_size(res) == 0) {
        if (res) csilk_json_free(res);
        return -1;
    }
    *out_id = db_get_int(csilk_json_array_get(res, 0), "id");
    csilk_json_free(res);
    return 0;
}

int
mf_tag_repo_update(void* db_pool, int64_t user_id, int64_t id, const char* name, const char* color)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "UPDATE tags SET name=?, color=? WHERE id=? AND user_id=? RETURNING id",
        (const char*[]){name, color && color[0] ? color : "#409EFF", id_str, uid_str, NULL});

    if (res) csilk_json_free(res);
    return 0;
}

int
mf_tag_repo_delete(void* db_pool, int64_t user_id, int64_t id)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char uid_str[32], id_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);

    return csilk_db_query_param_json(
        pool,
        "DELETE FROM tags WHERE id=? AND user_id=?",
        (const char*[]){id_str, uid_str, NULL}) ? 0 : -1;
}

void
mf_tag_repo_free_list(mf_tag_t* list, size_t count)
{
    (void)count;
    free(list);
}
```

- [ ] **Step 7: 改造 tag_controller.c 为新架构**

```c
// backend/src/interfaces/http/controllers/tag_controller.c
#include "interfaces/http/controllers/tag_controller.h"
#include "application/tag/usecases.h"
#include "application/tag/commands.h"
#include "common/response.h"
#include "common/ctx.h"
#include "common/db.h"

void
api_tag_list(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) return;

    csilk_json_t* list = NULL;
    if (tag_usecase_list(db_get_pool(), user_id, &list) != 0) {
        respond_error(c, 500, "查询标签列表失败");
        return;
    }
    respond_ok(c, list ? list : csilk_json_array());
}

void
api_tag_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) return;

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    create_tag_cmd_t cmd = {
        .user_id = user_id,
        .name = csilk_json_get_string(body, "name"),
        .color = csilk_json_get_string(body, "color"),
    };

    int64_t new_id = 0;
    tag_usecase_result_t res = {0};
    int rc = tag_usecase_create(db_get_pool(), &cmd, &new_id, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        csilk_json_t* d = csilk_json_object();
        csilk_json_add_number(d, "id", (double)new_id);
        respond_ok(c, d);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_tag_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_json_t* body = csilk_bind_json(c);
    if (!body) { respond_bad_request(c, "请求体必须为 JSON"); return; }

    update_tag_cmd_t cmd = {
        .user_id = user_id,
        .tag_id = atoll(id_str),
        .name = csilk_json_get_string(body, "name"),
        .color = csilk_json_get_string(body, "color"),
    };

    tag_usecase_result_t res = {0};
    int rc = tag_usecase_update(db_get_pool(), &cmd, &res);
    csilk_json_free(body);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void
api_tag_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id <= 0) return;

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    delete_tag_cmd_t cmd = { .user_id = user_id, .tag_id = atoll(id_str) };
    tag_usecase_result_t res = {0};
    int rc = tag_usecase_delete(db_get_pool(), &cmd, &res);

    if (rc == 0 && res.code == 0) {
        respond_ok_null(c);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}

void register_tag_routes(csilk_app_t* app) {
    // 路由注册保持不变，只改 handler 函数指针
    csilk_app_get_ext(app, "/api/tags", api_tag_list, NULL, NULL, "List tags", "...");
    csilk_app_post_ext(app, "/api/tags", api_tag_create, NULL, NULL, "Create tag", "...");
    csilk_app_put_ext(app, "/api/tags/:id", api_tag_update, NULL, NULL, "Update tag", "...");
    csilk_app_delete_ext(app, "/api/tags/:id", api_tag_delete, NULL, NULL, "Delete tag", "...");
}
```

- [ ] **Step 8: 编译验证**

```bash
cd backend && cmake --build build --parallel
```

- [ ] **Step 9: 运行测试**

```bash
cd backend/build && ctest --output-on-failure
```

- [ ] **Step 10: Commit**

---

### Task 2: Category 域迁移

**Files:**
- Create: `backend/src/domain/category/entity.h`
- Create: `backend/src/domain/category/repository.h`
- Create: `backend/src/domain/category/rules.h`
- Create: `backend/src/domain/category/rules.c`
- Create: `backend/src/application/category/commands.h`
- Create: `backend/src/application/category/dtos.h`
- Create: `backend/src/application/category/usecases.h`
- Create: `backend/src/application/category/usecases.c`
- Create: `backend/src/infrastructure/repositories/category_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/category_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/category_controller.c`

- [ ] **Step 1: 创建 Domain Entity**

```c
// backend/src/domain/category/entity.h
#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    char     name[128];
    int64_t  parent_id;
    char     type[32];        // "asset", "income", "expense", "transaction"
    char     asset_type[32];  // "cash", "stock", "fund", etc.
    char     currency[8];
    char     icon[64];
    int      sort_order;
    char     created_at[32];
    char     updated_at[32];
} mf_category_t;
```

- [ ] **Step 2: 创建 Domain Repository Contract**

```c
// backend/src/domain/category/repository.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "domain/category/entity.h"

int mf_category_repo_list(void* db_pool, int64_t user_id, mf_category_t** out_list, size_t* out_count);
int mf_category_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_category_t* out);
int mf_category_repo_create(void* db_pool, int64_t user_id, const mf_category_t* cat, int64_t* out_id);
int mf_category_repo_update(void* db_pool, int64_t user_id, int64_t id, const mf_category_t* cat);
int mf_category_repo_delete(void* db_pool, int64_t user_id, int64_t id);
int mf_category_repo_has_children(void* db_pool, int64_t user_id, int64_t id);
int mf_category_repo_has_assets(void* db_pool, int64_t user_id, int64_t id);
void mf_category_repo_free_list(mf_category_t* list, size_t count);
```

- [ ] **Step 3: 创建 Domain Rules**

```c
// backend/src/domain/category/rules.h
#pragma once
#include <stdbool.h>

bool mf_category_rule_validate_name(const char* name);
bool mf_category_rule_validate_type(const char* type);
bool mf_category_rule_validate_asset_type(const char* asset_type);
bool mf_category_rule_can_delete(bool has_children, bool has_assets);
```

- [ ] **Step 4: 创建 Application 层**

```c
// backend/src/application/category/usecases.h
#pragma once
#include "csilk/csilk.h"
#include "application/category/commands.h"
#include "application/category/dtos.h"

int category_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_tree);
int category_usecase_create(void* pool, const create_category_cmd_t* cmd, int64_t* out_id, category_usecase_result_t* out_res);
int category_usecase_update(void* pool, const update_category_cmd_t* cmd, category_usecase_result_t* out_res);
int category_usecase_delete(void* pool, int64_t user_id, int64_t id, category_usecase_result_t* out_res);
```

- [ ] **Step 5: 创建 Infrastructure Repository Implementation**

参照 Task 1 的模式实现 `category_repo_impl.c`。

- [ ] **Step 6: 改造 category_controller.c 为新架构**

- [ ] **Step 7: 编译验证 + 测试 + Commit**

---

### Task 3: Ledger 域迁移

**Files:**
- Create: `backend/src/domain/ledger/entity.h`
- Create: `backend/src/domain/ledger/repository.h`
- Create: `backend/src/domain/ledger/rules.h`
- Create: `backend/src/domain/ledger/rules.c`
- Create: `backend/src/application/ledger/commands.h`
- Create: `backend/src/application/ledger/dtos.h`
- Create: `backend/src/application/ledger/usecases.h`
- Create: `backend/src/application/ledger/usecases.c`
- Create: `backend/src/infrastructure/repositories/ledger_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/ledger_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/ledger_controller.c`

- [ ] **Step 1-6:** 参照 Task 1 模式，重点处理 RBAC 权限校验（`ledger_get_user_role` → `ledger_rules_check_permission`）

- [ ] **Step 7: 编译验证 + 测试 + Commit**

---

## Phase 2: 业务编排域

### Task 4: Daily Expense 域迁移

**Files:**
- Create: `backend/src/domain/daily_expense/entity.h`
- Create: `backend/src/domain/daily_expense/repository.h`
- Create: `backend/src/domain/daily_expense/rules.h`
- Create: `backend/src/domain/daily_expense/rules.c`
- Create: `backend/src/application/daily_expense/commands.h`
- Create: `backend/src/application/daily_expense/dtos.h`
- Create: `backend/src/application/daily_expense/usecases.h`
- Create: `backend/src/application/daily_expense/usecases.c`
- Create: `backend/src/infrastructure/repositories/daily_expense_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/daily_expense_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/daily_expense_controller.c`

- [ ] **Step 1-6:** 参照 Task 1 模式。daily_expense 需要处理 tags 关联（多对多）和 balance_apply_delta。

- [ ] **Step 7: 编译验证 + 测试 + Commit**

---

### Task 5: Transfer 域迁移

**Files:**
- Create: `backend/src/domain/transfer/entity.h`
- Create: `backend/src/domain/transfer/repository.h`
- Create: `backend/src/domain/transfer/rules.h`
- Create: `backend/src/domain/transfer/rules.c`
- Create: `backend/src/application/transfer/commands.h`
- Create: `backend/src/application/transfer/dtos.h`
- Create: `backend/src/application/transfer/usecases.h`
- Create: `backend/src/application/transfer/usecases.c`
- Create: `backend/src/infrastructure/repositories/transfer_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/transfer_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/transfer_controller.c`

- [ ] **Step 1: 创建 Domain Entity**

```c
// backend/src/domain/transfer/entity.h
#pragma once
#include <stdint.h>
#include "core/financial/money.h"

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  from_asset_id;
    int64_t  to_asset_id;
    money_t  amount;
    char     currency[8];
    char     transfer_date[32];
    char     note[256];
} mf_transfer_t;
```

- [ ] **Step 2: 创建 Domain Rules**

```c
// backend/src/domain/transfer/rules.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool mf_transfer_rule_validate(int64_t from_id, int64_t to_id, double amount, const char* date);
```

- [ ] **Step 3: 创建 Application Use Cases**

transfer controller 中的业务逻辑（BEGIN → insert transfer → insert tx_out → insert tx_in → ledger_apply_transfer → COMMIT）应下沉到 `transfer_usecase_create`。

- [ ] **Step 4-6:** 实现 Infrastructure Repo + 改造 Controller

- [ ] **Step 7: 编译验证 + 测试 + Commit**

---

### Task 6: DCA 域迁移

**Files:**
- Create: `backend/src/domain/dca/entity.h`
- Create: `backend/src/domain/dca/repository.h`
- Create: `backend/src/domain/dca/rules.h`
- Create: `backend/src/domain/dca/rules.c`
- Create: `backend/src/application/dca/commands.h`
- Create: `backend/src/application/dca/dtos.h`
- Create: `backend/src/application/dca/usecases.h`
- Create: `backend/src/application/dca/usecases.c`
- Create: `backend/src/infrastructure/repositories/dca_repo_impl.c`
- Create: `backend/src/infrastructure/repositories/dca_repo_impl.h`
- Modify: `backend/src/interfaces/http/controllers/dca_controller.c`

- [ ] **Step 1-6:** 参照 Task 5 模式。dca_controller 中的 `dca_service_confirm_execution` 含 BEGIN/COMMIT 事务和 `ledger_apply_tx` 调用，需完整下沉到 usecase。

- [ ] **Step 7: 编译验证 + 测试 + Commit**

---

## Phase 3: 报表查询域

### Task 7: Report 域迁移

**Files:**
- Create: `backend/src/domain/report/rules.h`
- Create: `backend/src/domain/report/rules.c`
- Create: `backend/src/application/report/usecases.h`
- Create: `backend/src/application/report/usecases.c`
- Modify: `backend/src/interfaces/http/controllers/report_controller.c`

- [ ] **Step 1:** Report 域为纯查询，Domain 层仅定义查询参数校验规则（如月份格式、趋势月数上限）
- [ ] **Step 2:** Application Use Cases 编排 legacy report service 中的查询逻辑（或直接在 usecase 中实现，因为是纯查询）
- [ ] **Step 3:** 改造 report_controller.c
- [ ] **Step 4: 编译验证 + 测试 + Commit**

---

## Phase 4: 辅助域

### Task 8: File/Import-Export/ImportRule/Receipt 域迁移

这些域相对独立，按相同模式逐个迁移：

- [ ] **Step 1:** File 域（文件上传/解析）
- [ ] **Step 2:** Import/Export 域（CSV 导入导出）
- [ ] **Step 3:** Import Rule 域（智能分类规则）
- [ ] **Step 4:** Receipt 域（OCR 识别）
- [ ] **Step 5: 编译验证 + 测试 + Commit**

---

## Phase 5: 清理

### Task 9: 删除废弃文件

**Files:**
- Delete: `backend/src/controllers/` (整个目录)
- Delete: `backend/src/services/daily_expense_query.c`
- Delete: `backend/src/services/daily_expense_write.c`
- Delete: `backend/src/services/report_expense_service.c`
- Delete: `backend/src/services/report_asset_service.c`
- Delete: `backend/src/services/report_holdings_service.c`
- Delete: `backend/src/services/category_service.c`
- Delete: `backend/src/services/export_service.c`
- Delete: `backend/src/services/import_service.c`
- Delete: `backend/src/services/file_parser.c`
- Delete: `backend/src/repositories/daily_expense_repo.c`
- Delete: `backend/src/repositories/transfer_repo.c`
- Delete: `backend/src/repositories/dca_repo.c`
- Delete: `backend/src/repositories/category_repo.c`
- Delete: `backend/src/repositories/tag_repo.c`
- Delete: `backend/src/repositories/ledger_repo.c`
- Delete: `backend/src/repositories/import_rule_repo.c`
- Delete: `backend/src/repositories/asset_repo.c`
- Delete: `backend/src/repositories/auth_repo.c`
- Delete: `backend/src/repositories/transaction_repo.c`
- Delete: `backend/src/repositories/cashflow_repo.c`
- Delete: `backend/src/repositories/price_history_repo.c`

- [ ] **Step 1:** 确认所有新架构 usecase 已接管对应功能
- [ ] **Step 2:** 删除废弃文件
- [ ] **Step 3:** 编译验证（确认无未解析符号）
- [ ] **Step 4:** 运行全量测试
- [ ] **Step 5:** 更新 `CMakeLists.txt`（如有硬编码源文件列表）
- [ ] **Step 6:** 更新 `AGENTS.md` 架构文档
- [ ] **Step 7:** Commit

---

## 验证清单

每个 Task 完成后必须验证：

```bash
# 1. 编译
cd backend && cmake --build build --parallel

# 2. 单元测试
cd backend/build && ctest --output-on-failure

# 3. 集成测试
cd backend && ./tests/test_link.sh
cd backend && ./tests/test_ledgers.sh
cd backend && ./tests/test_2fa.sh
cd backend && ./tests/test_dca_cashflow.sh

# 4. 前端构建（确认 API 契约不变）
cd frontend && npm run build
```

---

## 风险缓解

| 风险 | 缓解措施 |
|------|----------|
| API 契约破坏 | Controller 保持相同路径、参数、响应格式，仅内部调用链变化 |
| 事务原子性回归 | 每个 usecase 的 BEGIN/COMMIT/ROLLBACK 模式与原 service 一致 |
| 性能回归 | 基准测试：迁移前后对比 /api/transactions 和 /api/summary 响应时间 |
| Domain 层引入外部依赖 | 严格审查 `domain/*/` 目录下的 `#include`，禁止出现 `csilk/`、`repositories/` |
| 测试覆盖缺口 | 每个迁移域至少一个 CTest 用例验证 CRUD + 业务规则 |
