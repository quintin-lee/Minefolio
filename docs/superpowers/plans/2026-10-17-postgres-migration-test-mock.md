# PostgreSQL 迁移方言单元测试 Mock 解耦 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 `test_migration_engine.c` 在无本地 PostgreSQL 运行时的测试崩溃断言，使 CTest 33 个测试套件达成 100% 绿色全通。

**Architecture:** 
1. **驱动覆写支持** (`deps/csilk/src/drivers/db/db.c`)：允许 `csilk_db_register_driver()` 对已有同名驱动进行原地替换，赋能单元测试的 Mock 注入。
2. **测试现场隔离与复原** (`backend/tests/unit/test_migration_engine.c`)：在测试开始前备份原生驱动，注入 `s_mock_pg_driver`，验证 V001~V013 迁移脚本发现，并在退出时恢复原生驱动。

**Tech Stack:** C23, GCC 16 / Linux, csilk v0.5.2, CTest, CMake.

---

### File Structure Map

| 目标文件 | 责任与边界 |
|---|---|
| `deps/csilk/src/drivers/db/db.c` | 修改 `csilk_db_register_driver` 支持驱动原地更新 |
| `backend/tests/unit/test_migration_engine.c` | 备份原生驱动、注入 Mock、校准 V013 数量并在退出时复原 |

---

### Task 1: 框架层驱动注册表原地覆写能力实现与验证

**Files:**
- Modify: `deps/csilk/src/drivers/db/db.c:615-635`

- [x] **Step 1: 在 `deps/csilk/src/drivers/db/db.c` 中增加同名查找替换逻辑**

修改 `csilk_db_register_driver()`：
```c
int
csilk_db_register_driver(const char* name, csilk_db_driver_t* driver)
{
    if (!name || !driver) {
        CSILK_LOG_E("Failed to register database driver: name or driver is NULL");
        return -1;
    }
    ensure_registry_init();

    csilk_mutex_lock(&registry_mutex);
    for (int i = 0; i < driver_count; i++) {
        if (strcmp(drivers[i]->name, name) == 0) {
            drivers[i] = driver;
            CSILK_LOG_I("Overwrote database driver: '%s'", name);
            csilk_mutex_unlock(&registry_mutex);
            return 0;
        }
    }
    if (driver_count >= 16) {
        CSILK_LOG_E("Failed to register database driver '%s': registry is full", name);
        csilk_mutex_unlock(&registry_mutex);
        return -1;
    }
    drivers[driver_count++] = driver;
    CSILK_LOG_I("Registered database driver: '%s'", name);
    csilk_mutex_unlock(&registry_mutex);
    return 0;
}
```

- [x] **Step 2: 编译 csilk 静态库确保编译通过**

```bash
cmake --build backend/build --target csilk_db
```
Expected: PASS。

- [x] **Step 3: 提交 `deps/csilk` 变更**

```bash
git -C deps/csilk add src/drivers/db/db.c
git -C deps/csilk commit -m "fix(db): 🐛 support in-place driver overwrite in csilk_db_register_driver"
```

---

### Task 2: 迁移单元测试现场隔离、断言校准与驱动复原

**Files:**
- Modify: `backend/tests/unit/test_migration_engine.c:285-308`

- [x] **Step 1: 修改 `test_postgres_migration_dialect`**

备份原生驱动并在退出时恢复，同时将期望的脚本数校准至 13：
```c
static void
test_postgres_migration_dialect(void)
{
    printf("--- Running PostgreSQL Migration Dialect Contract Test ---\n");

    csilk_db_driver_t* orig_driver = csilk_db_get_driver("postgres");

    csilk_db_register_driver("postgres", &s_mock_pg_driver);
    csilk_db_pool_t* pool = csilk_db_pool_new("postgres", "mock_dsn");
    assert(pool != NULL);

    csilk_migration_engine_t* engine = NULL;
    assert(csilk_migration_engine_new(pool, "sql/migrations", &engine) == 0 && engine != NULL);

    csilk_migration_item_t* items = NULL;
    int                     count = 0;
    assert(csilk_migration_discover(engine, &items, &count) == 0);
    assert(count >= 13);
    assert(strstr(items[0].filepath, "postgres") != NULL);

    free(items);
    csilk_migration_engine_free(engine);
    csilk_db_pool_free(pool);

    if (orig_driver) {
        csilk_db_register_driver("postgres", orig_driver);
    }
    printf("PostgreSQL migration dialect contract passed.\n");
}
```

- [x] **Step 2: 编译并单独运行 `test_migration_engine`**

```bash
cmake --build backend/build --target test_migration_engine && ./backend/build/test_migration_engine
```
Expected: PASS，控制台输出 `All Database Migration Engine Tests Passed (100%)!`。

- [x] **Step 3: 提交代码**

```bash
git add backend/tests/unit/test_migration_engine.c
git commit -m "test(migration): 🧪 isolate postgres dialect test with mock restoration"
```

---

### Task 3: CTest 全量验证与全绿回归

**Files:**
- Test: 全量 CTest 测试套件

- [x] **Step 1: 运行全量 CTest**

```bash
cd backend/build && ctest --output-on-failure
```
Expected: **100% tests passed, 0 tests failed out of 33**。

- [x] **Step 2: 提交计划文档**

```bash
git add docs/superpowers/plans/2026-10-17-postgres-migration-test-mock.md
git commit -m "docs: 📝 add implementation plan for postgres migration test mock isolation"
```
