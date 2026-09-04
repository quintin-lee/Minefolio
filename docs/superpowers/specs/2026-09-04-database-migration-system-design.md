# Minefolio P1-04：正式 Database Migration System 设计规范

## 1. 背景与目标

在既有实现中，数据库演进依赖于 `backend/src/common/db.c` 中超过 600 行的硬编码代码（反复检测列名、手动 `ALTER TABLE`、手动在 SQLite 中备份重命名旧表与数据搬迁）。
这种“渐进式检测字段 + ALTER TABLE”的方式存在显著缺陷：
1. **缺乏版本化追溯**：无法获知当前数据库处于哪个精确版本，也无法按序重放。
2. **缺乏防篡改与完整性校验**：无法发现历史变更被篡改或文件缺失。
3. **缺乏并发互斥保护**：多实例服务同时启动时可能发生迁移竞态与死锁。
4. **引擎硬编码耦合**：大量数据库引擎分支穿插在迁移代码中。

### P1-04 重构目标
- **目标 1**：建立结构化、版本化的迁移脚本目录 `sql/migrations/`（支持 SQLite / PostgreSQL 方言分离）。
- **目标 2**：建立标准迁移元数据表 `schema_migrations`，记录版本号、名称、SHA-256 校验和、执行时间及耗时。
- **目标 3**：实现原生 C 迁移引擎 `infrastructure/database/migration/`，具备 `discover`, `validate`, `apply`, `status`, `checksum`, `rollback` 完整能力。
- **目标 4**：实现迁移互斥锁（`schema_migration_lock` 及 PostgreSQL Advisory Lock），确保集群/多进程启动安全。
- **目标 5**：**无损兼容存量生产库（Auto-Baseline）**，严禁生产环境清空或重建数据库；对已有业务数据的数据库自动基线化。
- **目标 6**：彻底废除 `common/db.c` 中的 600+ 行硬编码列检测，全量测试 100% 保持通过。

---

## 2. 目录结构与迁移文件规范

### 2.1 目录组织
```text
backend/sql/migrations/
├── sqlite/
│   ├── V001__initial_auth_and_system.sql
│   ├── V002__categories_and_assets.sql
│   ├── V003__transactions_and_expenses.sql
│   ├── V004__ledgers_and_members.sql
│   ├── V005__ai_traces_and_settings.sql
│   ├── V006__dca_and_cashflow.sql
│   └── V007__market_quotes_and_fx.sql
└── postgres/
    ├── V001__initial_auth_and_system.sql
    ├── V002__categories_and_assets.sql
    ├── V003__transactions_and_expenses.sql
    ├── V004__ledgers_and_members.sql
    ├── V005__ai_traces_and_settings.sql
    ├── V006__dca_and_cashflow.sql
    └── V007__market_quotes_and_fx.sql
```

### 2.2 命名与语法规则
- 前缀：`V<version>__<description>.sql`（例如 `V001__initial_auth_and_system.sql`），其中 `<version>` 为正整数（支持前导 0，解析时按整数大小排序）。
- 可选回滚前缀：`U<version>__<description>.sql`（例如 `U002__rollback_categories.sql`）。
- 语义一致性：SQLite 与 PostgreSQL 的同版本号脚本在领域实体与外键语义上严格对应，语法遵循各自方言优化。

---

## 3. 数据库元数据表与并发锁设计

### 3.1 状态跟踪表 `schema_migrations`
```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    checksum TEXT NOT NULL,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    execution_time_ms INTEGER NOT NULL DEFAULT 0
);
```

### 3.2 互斥锁表 `schema_migration_lock`
```sql
CREATE TABLE IF NOT EXISTS schema_migration_lock (
    id INTEGER PRIMARY KEY,
    is_locked INTEGER NOT NULL DEFAULT 0,
    locked_at TIMESTAMP,
    locked_by TEXT
);
```

### 3.3 锁机制实现
1. **SQLite**：
   - 尝试原子更新：`UPDATE schema_migration_lock SET is_locked = 1, locked_at = CURRENT_TIMESTAMP, locked_by = ? WHERE id = 1 AND is_locked = 0;`
   - 配合独占事务与 `busy_timeout`，若无法获取锁则等待重试；超时后报错退出，防止并发损坏。
2. **PostgreSQL**：
   - 使用应用专用 64 位 Advisory Lock：`SELECT pg_try_advisory_lock(88481024);`
   - 迁移结束后调用 `SELECT pg_advisory_unlock(88481024);` 释放。

---

## 4. 迁移引擎架构设计 (`infrastructure/database/migration/`)

```text
infrastructure/database/migration/
├── migration_engine.h / .c  # 引擎主调度 (init, discover, validate, apply, status)
├── checksum.h / .c          # SHA-256 跨平台规范化哈希计算
└── lock.h / .c              # 跨引擎分布式互斥锁管理
```

### 4.1 核心数据结构与接口
```c
typedef struct {
    int   version;
    char  name[128];
    char  filepath[256];
    char  checksum[65];
    bool  is_applied;
    int   execution_time_ms;
} mf_migration_item_t;

typedef struct mf_migration_engine_s mf_migration_engine_t;

int  mf_migration_engine_new(mf_db_t* db, const char* migrations_dir, mf_migration_engine_t** out_engine);
void mf_migration_engine_free(mf_migration_engine_t* engine);

/* 引擎能力 */
int  mf_migration_discover(mf_migration_engine_t* engine, mf_migration_item_t** out_items, int* out_count);
int  mf_migration_validate(mf_migration_engine_t* engine);
int  mf_migration_apply(mf_migration_engine_t* engine, int* out_applied_count);
int  mf_migration_status(mf_migration_engine_t* engine, mf_migration_item_t** out_items, int* out_count);
```

### 4.2 校验和规范化算法 (`checksum.c`)
- 文件哈希采用 OpenSSL `SHA256()`。
- **跨平台换行符规范化**：读取 SQL 内容时，将所有 `\r\n` 规整转换为 `\n`，并去除尾部多余空白行后计算散列，防止跨平台 Git 检出导致校验和不匹配。

### 4.3 校验与事务执行 (`apply`)
- 对每一个待执行的迁移文件：
  1. 开启事务 `mf_tx_begin`；
  2. 读取 SQL 并执行（支持多语句执行）；
  3. 写入 `schema_migrations (version, name, checksum, execution_time_ms)`；
  4. 提交事务 `mf_tx_commit`；
  5. 若执行失败，立即执行 `mf_tx_rollback` 并中断流程。

---

## 5. 生产兼容与自动基线化 (Auto-Baseline)

### 5.1 存量数据库检测逻辑
在执行任何迁移动作之前：
1. 查询是否存在 `schema_migrations` 表。
2. 若 `schema_migrations` 不存在，执行存量探测查询（如 `SELECT COUNT(*) FROM users;` 或 `SELECT COUNT(*) FROM assets;`）。
3. 若探测成功（数据表已存在）：
   - **判定为存量生产数据库**；
   - 创建 `schema_migrations` 表与锁表；
   - 自动扫描基线版本集合（V001 ~ V007），为每个基线脚本计算当前本地校验和；
   - 直接向 `schema_migrations` 插入对应基线记录（`execution_time_ms = 0`, `applied_at = CURRENT_TIMESTAMP`）；
   - **绝不对存量数据库重复执行已存在的 DDL**，数据与表结构 100% 完整保留。
4. 若探测失败（全新空库）：
   - 创建 `schema_migrations` 表；
   - 按序完整执行 V001 ~ V007。

---

## 6. 与系统启动链路集成

- 改造 `backend/src/common/db.c` 中的 `db_run_migrations(pool)`：
  ```c
  int db_run_migrations(csilk_db_pool_t* pool) {
      mf_db_t* db = NULL;
      mf_db_wrap_csilk(pool, db_is_postgres() ? MF_DB_ENGINE_POSTGRES : MF_DB_ENGINE_SQLITE, &db);
      
      mf_migration_engine_t* engine = NULL;
      const char* dir = "sql/migrations"; // 或根据运行目录寻址
      mf_migration_engine_new(db, dir, &engine);
      
      int applied_count = 0;
      int rc = mf_migration_apply(engine, &applied_count);
      
      mf_migration_engine_free(engine);
      mf_db_close(db);
      return rc;
  }
  ```
- 彻底删除 `db.c` 中原有的 600+ 行手动列检测与表重写代码。

---

## 7. 测试与验证策略

### 7.1 独立单元测试 (`tests/unit/test_migration_engine.c`)
1. **文件发现与解析**：验证文件名解析提取 version 与 name、版本递增排序。
2. **校验和一致性**：验证同一文件在不同换行符下计算得到相同 SHA-256。
3. **空库全量应用**：在内存库中完整执行 V001~V007，校验表结构完备且 `schema_migrations` 记录数等于 7。
4. **防篡改校验 (Tamper Detection)**：人为修改已应用迁移文件内容，验证 `validate` 正确报错拦截。
5. **存量自动基线化测试**：模拟已有 `users` 表的存量库，验证引擎自动标记基线而不报错、不破坏已有数据。
6. **互斥锁测试**：验证持锁状态下并发迁移被安全阻止。

### 7.2 全量集成与回归测试
- 确保全部 25+ 项 CTest 单元测试 100% PASS。
- 确保全部 7 大集成测试套件（`test_link.sh`, `test_ledgers.sh` 等 139 项断言）全部 100% PASS。
- 前端 Vitest 与生产构建零错误。
