# csilk 功能下沉与 Minefolio 适配重构规划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Minefolio 中成熟的通用基础设施与协议组件下沉至 `../csilk` 框架，补齐 csilk 生产级功能缺口（迁移治理、事务保存点、预编译绑定、中间件白名单、TOTP 2FA、MCP 进程池、Tool-Calling ReAct 循环等），随后重构 Minefolio 移除冗余胶水代码并接入 csilk 新 API。

**Architecture:** 
1. **csilk 框架层**（位于 `../csilk`）：遵循 C23 标准与模块化架构扩展：
   - `csilk_core` / `csilk_json`：JSON 智能类型提取 (`as_int64`, `as_double`, `as_bool`)、多层级 Secret Provider；
   - `csilk_middleware`：CSRF 与 JWT 中间件增加路由白名单与排除谓词；JWT 增加状态吊销钩子与 TTL 缓存；
   - `csilk_crypto`：RFC 6238 标准 TOTP (2FA) 认证引擎；
   - `csilk_util`：RFC 4180 标准 CSV 流式解析与转义器；
   - `csilk_db`：Flyway 风格版本迁移引擎 (`csilk_db_migration`)、保存点作用域管理 (`csilk_db_tx_scope`)、真实预编译语句 (`csilk_stmt`)；
   - `csilk_protocols`：MCP STDIO 子进程长连接池 (`csilk_mcp_stdio_pool`)；
   - `csilk_workflow` / `csilk_ai`：原生 Function Calling ReAct 循环、Tool Registry 与全链路 AI Trace。
2. **Minefolio 应用层**（位于当前工程）：将 `../deps/csilk` 升级同步后，移除 `infrastructure/database/migration/`、`common/totp.*`、`common/csv_utils.*`、`middlewares/*_wrapper`，直接对接 csilk 官方 API。

**Tech Stack:** C23, CMake 3.16+, libuv, SQLite3, libpq (PostgreSQL), OpenSSL (Crypto/TOTP), yyjson/cJSON, CTest.

---

## 阶段一：基础工具与中间件零阻力标准件 (Phase 1)

### Task 1: JSON 智能安全类型转换 (`csilk_json_as_*`)

解决 csilk 中 `csilk_db_query_json` 将列默认映射为字符串节点，导致直接调用 `csilk_json_get_number()` 返回 `0.0` 的陷阱。

**Files:**
- Modify: `../csilk/include/csilk/core/json.h`
- Modify: `../csilk/src/core/json/json_access.c`
- Create: `../csilk/tests/core/test_json_as.c`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 在 `../csilk/tests/core/test_json_as.c` 编写失败测试**

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "csilk/core/json.h"

int main(void) {
    csilk_json_t* obj = csilk_json_object();
    csilk_json_add_string(obj, "str_int", "42");
    csilk_json_add_string(obj, "str_double", "3.14159");
    csilk_json_add_string(obj, "str_bool_t", "true");
    csilk_json_add_string(obj, "str_bool_f", "false");
    csilk_json_add_string(obj, "str_dirty", "123abc");
    csilk_json_add_number(obj, "num_val", 100);
    csilk_json_add_bool(obj, "bool_val", true);

    /* 验证数字转换 */
    assert(csilk_json_as_int64(csilk_json_get(obj, "str_int"), 0) == 42);
    assert(csilk_json_as_int64(csilk_json_get(obj, "num_val"), 0) == 100);
    assert(csilk_json_as_int64(csilk_json_get(obj, "str_dirty"), -1) == -1); /* 脏数据安全回退 */
    assert(csilk_json_as_int64(csilk_json_get(obj, "non_exist"), -99) == -99);

    /* 验证浮点转换 */
    assert(csilk_json_as_double(csilk_json_get(obj, "str_double"), 0.0) > 3.14);
    assert(csilk_json_as_double(csilk_json_get(obj, "str_dirty"), -1.0) == -1.0);

    /* 验证布尔转换 */
    assert(csilk_json_as_bool(csilk_json_get(obj, "str_bool_t"), false) == true);
    assert(csilk_json_as_bool(csilk_json_get(obj, "str_bool_f"), true) == false);
    assert(csilk_json_as_bool(csilk_json_get(obj, "bool_val"), false) == true);

    csilk_json_free(obj);
    printf("test_json_as passed!\n");
    return 0;
}
```

- [x] **Step 2: 注册测试并运行以确认失败**

在 `../csilk/cmake/tests.cmake` 的 `CSILK_CORE_TESTS` 列表中加入 `test_json_as`。
运行命令：
```bash
cmake -B ../csilk/build -S ../csilk && cmake --build ../csilk/build --target test_json_as
```
预期结果：编译失败，提示 `csilk_json_as_int64` 等函数未声明。

- [x] **Step 3: 实现 `csilk_json_as_*` 核心逻辑**

在 `../csilk/include/csilk/core/json.h` 声明：
```c
int64_t csilk_json_as_int64(const csilk_json_t* val, int64_t default_val);
double  csilk_json_as_double(const csilk_json_t* val, double default_val);
bool    csilk_json_as_bool(const csilk_json_t* val, bool default_val);
```

在 `../csilk/src/core/json/json_access.c` 实现（基于 `strtod` / `strtoll` 严格全串消费校验）：
```c
int64_t csilk_json_as_int64(const csilk_json_t* val, int64_t default_val) {
    if (!val) return default_val;
    if (csilk_json_is_number(val)) return (int64_t)csilk_json_number_value(val);
    if (csilk_json_is_bool(val)) return csilk_json_bool_value(val) ? 1 : 0;
    if (csilk_json_is_string(val)) {
        const char* s = csilk_json_string_value(val);
        if (!s || !*s) return default_val;
        char* end = NULL;
        errno = 0;
        long long v = strtoll(s, &end, 10);
        if (errno != 0 || end == s || *end != '\0') return default_val;
        return (int64_t)v;
    }
    return default_val;
}

double csilk_json_as_double(const csilk_json_t* val, double default_val) {
    if (!val) return default_val;
    if (csilk_json_is_number(val)) return csilk_json_number_value(val);
    if (csilk_json_is_bool(val)) return csilk_json_bool_value(val) ? 1.0 : 0.0;
    if (csilk_json_is_string(val)) {
        const char* s = csilk_json_string_value(val);
        if (!s || !*s) return default_val;
        char* end = NULL;
        errno = 0;
        double v = strtod(s, &end);
        if (errno != 0 || end == s || *end != '\0') return default_val;
        return v;
    }
    return default_val;
}

bool csilk_json_as_bool(const csilk_json_t* val, bool default_val) {
    if (!val) return default_val;
    if (csilk_json_is_bool(val)) return csilk_json_bool_value(val);
    if (csilk_json_is_number(val)) return csilk_json_number_value(val) != 0.0;
    if (csilk_json_is_string(val)) {
        const char* s = csilk_json_string_value(val);
        if (!s || !*s) return default_val;
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "t") == 0 || strcmp(s, "1") == 0) return true;
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "f") == 0 || strcmp(s, "0") == 0) return false;
    }
    return default_val;
}
```

- [x] **Step 4: 编译并运行单测验证**

```bash
cmake --build ../csilk/build --target test_json_as && ctest --test-dir ../csilk/build -R test_json_as --output-on-failure
```
预期结果：`test_json_as` 100% 通过。

- [x] **Step 5: 提交更改**

```bash
git -C ../csilk add include/csilk/core/json.h src/core/json/json_access.c tests/core/test_json_as.c cmake/tests.cmake
git -C ../csilk commit -m "feat(json): 🎯 add csilk_json_as_* type coercion accessors"
```

---

### Task 2: 中间件路径白名单与跳过谓词机制 (CSRF & JWT)

**Files:**
- Modify: `../csilk/include/csilk/core/middleware.h`
- Modify: `../csilk/src/middleware/csrf.c`
- Modify: `../csilk/src/middleware/jwt.c`
- Create: `../csilk/tests/middleware/test_csrf_whitelist.c`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写白名单跳过测试**

在 `../csilk/tests/middleware/test_csrf_whitelist.c` 中构造 mock context，测试注册有白名单前缀 `"/api/auth/"` 时，对未携带 token 的 POST 请求予以放行（调用 `csilk_next` 而非 403 阻断）。

- [x] **Step 2: 运行测试验证失败**

```bash
cmake --build ../csilk/build --target test_csrf_whitelist
```
预期：编译失败，缺少 `csilk_csrf_config_t.exclude_prefixes` 字段。

- [x] **Step 3: 修改中间件配置接口与匹配逻辑**

在 `../csilk/include/csilk/core/middleware.h` 中升级配置体：
```c
typedef struct {
    const char* cookie_name;
    const char* header_name;
    const char** exclude_paths;    /**< NULL 结尾的全路径白名单 */
    const char** exclude_prefixes; /**< NULL 结尾的前缀白名单 */
    bool (*skip_fn)(csilk_ctx_t* c);
} csilk_csrf_config_t;

void csilk_csrf_middleware_ext(csilk_ctx_t* c, const csilk_csrf_config_t* config);
```
在 `../csilk/src/middleware/csrf.c` 和 `../csilk/src/middleware/jwt.c` 入口加入白名单匹配：
```c
if (config && config->exclude_prefixes) {
    const char* path = csilk_get_path(c);
    for (int i = 0; config->exclude_prefixes[i]; i++) {
        if (strncmp(path, config->exclude_prefixes[i], strlen(config->exclude_prefixes[i])) == 0) {
            csilk_next(c);
            return;
        }
    }
}
```

- [x] **Step 4: 编译并运行单测验证**

```bash
cmake --build ../csilk/build --target test_csrf_whitelist && ctest --test-dir ../csilk/build -R test_csrf_whitelist --output-on-failure
```

- [x] **Step 5: 提交更改**

```bash
git -C ../csilk add include/csilk/core/middleware.h src/middleware/csrf.c src/middleware/jwt.c tests/middleware/test_csrf_whitelist.c cmake/tests.cmake
git -C ../csilk commit -m "feat(middleware): 🎯 add route whitelist and skip predicates to CSRF and JWT"
```

---

### Task 3: RFC 6238 TOTP (2FA) 认证模块下沉

**Files:**
- Create: `../csilk/include/csilk/core/totp.h`
- Create: `../csilk/src/crypto/totp.c`
- Create: `../csilk/tests/crypto/test_totp.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写 TOTP 规范测试**

测试 RFC 6238 官方测试向量（不同 Unix 时间戳生成的预期 6 位动态验证码）与 ±30s 容错校验。

- [x] **Step 2: 验证测试失败**

```bash
cmake --build ../csilk/build --target test_totp
```

- [x] **Step 3: 从 Minefolio 迁移 `totp.c/.h` 到 csilk 并命名规范化**

将 Minefolio 的 `backend/src/common/totp.c/.h` 迁移至 `../csilk/include/csilk/core/totp.h` 与 `../csilk/src/crypto/totp.c`，重命名对外接口前缀为 `csilk_totp_*`：
- `csilk_totp_generate_secret`
- `csilk_totp_generate_code`
- `csilk_totp_verify_code`
- `csilk_totp_build_uri`
- `csilk_totp_generate_backup_codes`
- `csilk_totp_verify_backup_code`

在 `../csilk/cmake/sources.cmake` 的 `CSILK_CORE_SOURCES` 中追加 `src/crypto/totp.c`。

- [x] **Step 4: 编译并运行单测**

```bash
cmake --build ../csilk/build --target test_totp && ctest --test-dir ../csilk/build -R test_totp --output-on-failure
```

- [x] **Step 5: 提交更改**

```bash
git -C ../csilk add include/csilk/core/totp.h src/crypto/totp.c tests/crypto/test_totp.c cmake/sources.cmake cmake/tests.cmake
git -C ../csilk commit -m "feat(crypto): 🎯 add RFC 6238 TOTP 2FA engine"
```

---

### Task 4: RFC 4180 CSV 序列化与流式解析器

**Files:**
- Create: `../csilk/include/csilk/util/csv.h`
- Create: `../csilk/src/util/csv.c`
- Create: `../csilk/tests/core/test_csv.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写 CSV 规范测试**

覆盖换行符、逗号、双引号转义（`""`）以及空字段解析。

- [x] **Step 2: 迁移并封装 `csilk_csv_*` 接口**

从 Minefolio `backend/src/common/csv_utils.c/.h` 提取并重构为：
- `csilk_csv_escape(char* out, size_t out_sz, const char* in)`
- `csilk_csv_parse_field(...)`
- `csilk_csv_parse_row(...)`

- [x] **Step 3: 运行单测并通过**

```bash
cmake --build ../csilk/build --target test_csv && ctest --test-dir ../csilk/build -R test_csv --output-on-failure
```

- [x] **Step 4: 提交**

```bash
git -C ../csilk add include/csilk/util/csv.h src/util/csv.c tests/core/test_csv.c cmake/sources.cmake cmake/tests.cmake
git -C ../csilk commit -m "feat(util): 🎯 add RFC 4180 CSV escaping and parsing engine"
```

---

## 阶段二：生产级企业数据库与配置基础设施 (Phase 2)

### Task 5: 嵌套事务与保存点作用域管理 (`csilk_db_tx_scope`)

**Files:**
- Modify: `../csilk/include/csilk/drivers/db.h`
- Create: `../csilk/src/drivers/db/tx_scope.c`
- Create: `../csilk/tests/drivers/db/test_db_tx_scope.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写嵌套事务与保存点回滚测试**

编写对 SQLite 内存库连续开启两个 scope：外层提交、内层回滚测试；验证保存点部分回滚后外层仍能正常 commit。

- [x] **Step 2: 在 `csilk/drivers/db.h` 增加接口**

```c
typedef struct {
    char name[64];
    bool is_savepoint;
    bool active;
} csilk_db_tx_scope_t;

int csilk_db_in_transaction(csilk_db_pool_t* pool);
int csilk_db_tx_scope_begin(csilk_db_pool_t* pool, const char* name, csilk_db_tx_scope_t* scope);
int csilk_db_tx_scope_commit(csilk_db_pool_t* pool, csilk_db_tx_scope_t* scope);
int csilk_db_tx_scope_rollback(csilk_db_pool_t* pool, csilk_db_tx_scope_t* scope);
```

- [x] **Step 3: 实现 `tx_scope.c` 并在 sources.cmake 注册**

自动判断底层连接是否已处于事务块中，无则 `BEGIN TRANSACTION`，有则执行 `SAVEPOINT <name>`。

- [x] **Step 4: 运行测试并提交**

```bash
cmake --build ../csilk/build --target test_db_tx_scope && ctest --test-dir ../csilk/build -R test_db_tx_scope --output-on-failure
git -C ../csilk commit -m "feat(db): 🎯 add nested transaction scope and savepoint guard"
```

---

### Task 6: 预编译语句与强类型参数绑定 (`csilk_stmt`)

**Files:**
- Modify: `../csilk/include/csilk/drivers/db.h`
- Modify: `../csilk/src/drivers/db/sqlite.c`
- Modify: `../csilk/src/drivers/db/postgres.c`
- Create: `../csilk/tests/drivers/db/test_db_stmt.c`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写针对 SQLite 预编译语句的二进制绑定单测**

测试 `bind_int64`, `bind_double`, `bind_text`, `bind_null` 并执行 `SELECT` 与 `INSERT`。

- [x] **Step 2: 在 `csilk/drivers/db.h` 增加语句对象与驱动虚函数**

```c
typedef struct csilk_stmt_s csilk_stmt_t;

int csilk_stmt_prepare(csilk_db_pool_t* pool, const char* sql, csilk_stmt_t** out_stmt);
int csilk_stmt_bind_int64(csilk_stmt_t* stmt, int index, int64_t val);
int csilk_stmt_bind_double(csilk_stmt_t* stmt, int index, double val);
int csilk_stmt_bind_text(csilk_stmt_t* stmt, int index, const char* val);
int csilk_stmt_bind_null(csilk_stmt_t* stmt, int index);
int csilk_stmt_execute(csilk_stmt_t* stmt, int64_t* out_affected_rows);
csilk_json_t* csilk_stmt_query_json(csilk_stmt_t* stmt);
void csilk_stmt_close(csilk_stmt_t* stmt);
```

- [x] **Step 3: 在 `sqlite.c` 与 `postgres.c` 实现真实 Prepared Statement**

对接 `sqlite3_stmt` 与 libpq `PQprepare` / `PQexecPrepared`。

- [x] **Step 4: 运行测试并提交**

```bash
cmake --build ../csilk/build --target test_db_stmt && ctest --test-dir ../csilk/build -R test_db_stmt --output-on-failure
git -C ../csilk commit -m "feat(db): 🎯 implement native prepared statements and typed parameter bindings"
```

---

### Task 7: Flyway 风格数据库版本迁移引擎 (`csilk_db_migration`)

**Files:**
- Create: `../csilk/include/csilk/drivers/db/migration.h`
- Create: `../csilk/src/drivers/db/migration/migration_engine.c`
- Create: `../csilk/src/drivers/db/migration/migration_lock.c`
- Create: `../csilk/src/drivers/db/migration/migration_checksum.c`
- Create: `../csilk/tests/drivers/db/test_db_migration.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写迁移引擎集成测试**

测试创建临时迁移目录（`V001__init.sql`, `V002__add_col.sql`），执行迁移，篡改 checksum 触发校验失败，并发获取迁移锁等关键场景。

- [x] **Step 2: 迁移 Minefolio 核心迁移逻辑**

将 `backend/src/infrastructure/database/migration/` 整体重构移植至 csilk：
- `csilk_migration_engine_new(csilk_db_pool_t* pool, const char* dir, csilk_migration_engine_t** out)`
- `csilk_migration_validate(...)`
- `csilk_migration_apply(..., int* applied_count)`
- `csilk_migration_status(...)`

- [x] **Step 3: 运行单测并通过**

```bash
cmake --build ../csilk/build --target test_db_migration && ctest --test-dir ../csilk/build -R test_db_migration --output-on-failure
```

- [x] **Step 4: 提交**

```bash
git -C ../csilk add include/csilk/drivers/db/migration.h src/drivers/db/migration/ tests/drivers/db/test_db_migration.c cmake/sources.cmake cmake/tests.cmake
git -C ../csilk commit -m "feat(db): 🎯 add Flyway-style schema migration engine with distributed locks"
```

---

### Task 8: 云原生多层级 Secret Provider 与弱口令熔断

**Files:**
- Create: `../csilk/include/csilk/core/secret.h`
- Create: `../csilk/src/core/config/secret.c`
- Create: `../csilk/tests/core/test_secret.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写多层级凭据解析测试**

测试测试覆盖 > 环境变量 > 文件注入优先级，以及默认弱密码（`"change-me"`）在生产模式下的主动拦截。

- [x] **Step 2: 实现 `csilk_secret_get` 与配置加载**

提供 `csilk_secret_get(const char* key, char* out, size_t out_sz)`。

- [x] **Step 3: 编译测试并提交**

```bash
cmake --build ../csilk/build --target test_secret && ctest --test-dir ../csilk/build -R test_secret --output-on-failure
git -C ../csilk commit -m "feat(core): 🎯 add tiered cloud-native secret provider with safety circuit breakers"
```

---

### Task 9: MCP STDIO 子进程长连接池

**Files:**
- Modify: `../csilk/include/csilk/protocols/mcp.h`
- Create: `../csilk/src/protocols/mcp/mcp_stdio_pool.c`
- Create: `../csilk/tests/protocols/test_mcp_stdio_pool.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写 STDIO 客户端池化单测**

测试连续两次获取同一命令客户端返回同一有效 fd，以及超出 TTL 后的优雅回收。

- [x] **Step 2: 迁移 Minefolio `mcp_stdio_pool.c/.h` 并融入 csilk 原生 MCP 客户端**

将 `csilk_mcp_client_connect_stdio` 扩展为可由池接管：
- `csilk_mcp_stdio_pool_acquire(...)`
- `csilk_mcp_stdio_pool_release(...)`
- `csilk_mcp_stdio_pool_evict_idle(...)`

- [x] **Step 3: 运行单测并提交**

```bash
cmake --build ../csilk/build --target test_mcp_stdio_pool && ctest --test-dir ../csilk/build -R test_mcp_stdio_pool --output-on-failure
git -C ../csilk commit -m "feat(mcp): 🎯 add reusable stdio client process pool with graceful TTL reclamation"
```

---

## 阶段三：AI Agent 运行态升级 (Phase 3)

### Task 10: 原生 Tool-Calling ReAct 代理执行引擎与 Tool Registry

**Files:**
- Modify: `../csilk/include/csilk/app/workflow.h`
- Create: `../csilk/include/csilk/drivers/ai/tool_registry.h`
- Create: `../csilk/src/drivers/ai/tool_registry.c`
- Create: `../csilk/src/workflow/wf_ai_react_loop.c`
- Create: `../csilk/tests/workflow/test_ai_react_loop.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写 Mock LLM 下的 Tool-Calling ReAct 多轮循环测试**
- [x] **Step 2: 实现统一 Tool Registry（包含 JSON Schema 参数强校验）**
- [x] **Step 3: 实现基于大模型原生 `tool_calls` 结构体的动态 ReAct 循环与流式 Delta 桥接**
- [x] **Step 4: 编译通过并运行测试**
- [x] **Step 5: 提交**

```bash
git -C ../csilk commit -m "feat(workflow): 🎯 introduce native tool-calling ReAct agent execution loop"
```

---

### Task 11: AI 全链路 Trace 监控 (TTFT, Token, Cost, Tool Spans)

**Files:**
- Modify: `../csilk/include/csilk/drivers/ai.h`
- Create: `../csilk/src/drivers/ai/ai_trace.c`
- Create: `../csilk/tests/drivers/test_ai_trace.c`
- Modify: `../csilk/cmake/sources.cmake`
- Modify: `../csilk/cmake/tests.cmake`

- [x] **Step 1: 编写 TTFT 毫秒计时与 Token 吞吐率计算单测**
- [x] **Step 2: 实现 `csilk_ai_trace_t` 数据结构与 Span 树构建**
- [x] **Step 3: 运行测试并提交**

```bash
git -C ../csilk commit -m "feat(ai): 🎯 add end-to-end AI latency, token throughput, and tool span tracing"
```

---

## 阶段四：Minefolio 反向重构与接入验证 (Phase 4)

### Task 12: 更新 Minefolio 依赖并移除旧版胶水代码

**Files:**
- Modify: `backend/CMakeLists.txt`
- Modify: `backend/src/common/db.h` (使用 `csilk_json_as_*` 替换 `db_get_*`，使用 `csilk_db_tx_scope_*` 替换 `db_tx_scope_*`)
- Modify: `backend/src/middlewares/csrf_middleware.c` (直接使用带白名单配置的 `csilk_csrf_middleware_ext`)
- Modify: `backend/src/middlewares/jwt_middleware.c` (直接使用带白名单与吊销校验的 `csilk_jwt_middleware_options`)
- Delete: `backend/src/common/totp.c/.h` (替换为 `#include "csilk/core/totp.h"`)
- Delete: `backend/src/common/csv_utils.c/.h` (替换为 `#include "csilk/util/csv.h"`)
- Delete: `backend/src/infrastructure/database/migration/` (替换为 `#include "csilk/drivers/db/migration.h"`)

- [x] **Step 1: 更新 Minefolio 中 `deps/csilk` 代码镜像至最新 master**
- [x] **Step 2: 逐步替换业务代码中的 include 与调用**
- [x] **Step 3: 编译 Minefolio**

```bash
cmake --build backend/build --parallel
```
预期：编译 0 警告、0 错误。

- [x] **Step 4: 提交 Minefolio 重构改动**

```bash
git add backend/
git commit -m "refactor(core): ♻️ adapt downstreamed csilk features and remove redundant glue code"
```

---

### Task 13: Minefolio 全量测试套件回归验证

- [x] **Step 1: 运行全部 28 个 CTest 单元测试套件**

```bash
ctest --test-dir backend/build --output-on-failure
```
预期：28/28 测试通过（实际 32/32 测试通过）。

- [x] **Step 2: 运行全部 8 个端到端集成测试套件**

```bash
./backend/tests/test_link.sh
./backend/tests/test_ledgers.sh
./backend/tests/test_2fa.sh
./backend/tests/test_dca_cashflow.sh
./backend/tests/test_ai_trace.sh
./backend/tests/test_ai_tool_call.sh
./backend/tests/test_market_sync.sh
./backend/tests/test_fx_oauth.sh
```
预期：所有断言全部 PASS，零回归。

- [x] **Step 3: 运行前端构建与测试**

```bash
npm --prefix frontend test
npm --prefix frontend run build
```
预期：前端编译 0 错误。

