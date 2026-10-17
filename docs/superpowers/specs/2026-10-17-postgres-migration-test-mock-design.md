# PostgreSQL 迁移方言单元测试 Mock 解耦设计方案

- **日期**：2026-10-17
- **状态**：Approved (已评审通过)
- **目标范围**：`csilk_db_register_driver` 覆写能力支持、`test_migration_engine.c` PostgreSQL 方言隔离与驱动现场恢复

---

## 1. 背景与根因定位

在 `backend/tests/unit/test_migration_engine.c` 中，`test_postgres_migration_dialect()` 旨在测试当数据库连接池的驱动标识为 `"postgres"` 时，迁移引擎 `csilk_migration_discover()` 能否准确路由至 `sql/migrations/postgres/` 目录发现全部迁移脚本。

然而，在本地缺少 PostgreSQL 数据库守护进程的环境中，CTest 运行该用例时发生断言失败：
```text
test_migration_engine: test_postgres_migration_dialect: Assertion `pool != NULL' failed.
```

### 根本原因：
1. `test_migration_engine.c` 在 `main()` 中首先调用 `csilk_db_init()`，当构建环境存在 `libpq` 时，`csilk_db_init()` 自动在内置全局数组 `drivers[]` 中注册了官方原生 `csilk_db_postgres_driver`（索引位置靠前）。
2. `test_postgres_migration_dialect()` 中声明了桩驱动 `s_mock_pg_driver`，并通过 `csilk_db_register_driver("postgres", &s_mock_pg_driver)` 进行注册。
3. `csilk_db_register_driver()` 之前仅做追加写入（`drivers[driver_count++] = driver`），并不更新同名项。
4. `csilk_db_pool_new("postgres", ...)` 调用 `csilk_db_get_driver("postgres")` 进行线性查找时，永远命中排在首位的原生驱动，原生驱动发起真实的 `PQconnectdb` 网络连接，返回失败，导致连接池创建失败。

---

## 2. 详细设计

### 2.1 驱动注册表原地覆盖能力 (`deps/csilk/src/drivers/db/db.c`)

在框架层将 `csilk_db_register_driver()` 调整为“存在即覆盖，不存在则追加”：

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

### 2.2 测试上下文隔离与现场复原 (`test_migration_engine.c`)

在测试执行时：
1. 通过 `csilk_db_get_driver("postgres")` 备份原生驱动引用 `orig_driver`。
2. 注册并激活 `s_mock_pg_driver`，其 `connect()` 桩函数直接返回 `0`。
3. 执行连接池创建、迁移引擎初始化与脚本发现断言（校准数量 `count >= 13`）。
4. 无论测试结果如何，在清理阶段将 `orig_driver` 重新注册回全局表，保证后续测试不被 Mock 污染。

---

## 3. 验证计划

1. 编译 `deps/csilk` 与 `test_migration_engine` 测试目标。
2. 直接运行 `./backend/build/test_migration_engine`，确认 `test_postgres_migration_dialect` 成功通过。
3. 执行全量 CTest：`cd backend/build && ctest --output-on-failure`，验证 33 个测试套件 100% 全部通过。
