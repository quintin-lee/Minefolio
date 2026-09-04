# Minefolio P1-03：重构 Database / Repository Layer 设计规范

## 1. 背景与目标

在 Minefolio 架构演进中，P1-01 建立了七大领域与四层架构，P1-02 重构了 Asset 与 Portfolio 的纯净领域模型。
本阶段（P1-03）全面重构数据访问层（Database & Repository Layer）：
- **目标 1**：Repository 仅表达业务数据访问，不包含任何底层数据库引擎细节。
- **目标 2**：建立统一的底层数据库接口 `infrastructure/database/`，提供 `database.h`, `transaction.h`, `statement.h`，以及 `sqlite/` 与 `postgres/` 适配器。
- **目标 3**：全面落实 Repository **五大禁止**（禁业务规则、禁 HTTP、禁 AI workflow、禁权限决策、禁业务计算）。
- **目标 4**：消除业务代码与 Repository 中所有 `if (database_type == SQLITE)` 或 `db_is_postgres()` 分支，差异全部内聚于适配器。
- **目标 5**：提供完备的连接生命周期管理（线程归属、连接池、超时、重试），并支持标准事务与保存点（`savepoint`）。
- **目标 6**：编写独立测试，同一 Repository 测试同时运行于 SQLite 与 PostgreSQL，确保行为严格一致。

---

## 2. 整体架构设计

```text
┌────────────────────────────────────────────────────────────┐
│                    Application / Domain Layer              │
│    (Usecases, Domain Services, Ledger Engine, AI Tools)    │
└──────────────────────────────┬─────────────────────────────┘
                               │ (Calls Repository APIs)
                               ▼
┌────────────────────────────────────────────────────────────┐
│                    Repositories Layer                      │
│   repositories/user_repository                             │
│   repositories/asset_repository                            │
│   repositories/transaction_repository                      │
│   repositories/portfolio_repository                        │
│   repositories/price_history_repository                    │
│   ...                                                      │
└──────────────────────────────┬─────────────────────────────┘
                               │ (Uses Unified Database Interface)
                               ▼
┌────────────────────────────────────────────────────────────┐
│              infrastructure/database/                      │
│   ├── database.h     (mf_db_t, pooling, timeout, retry)   │
│   ├── transaction.h  (mf_tx_t, begin, commit, savepoint)  │
│   └── statement.h    (mf_stmt_t, typed bind, result set)  │
└──────────────┬───────────────────────────────┬─────────────┘
               │                               │
               ▼                               ▼
┌──────────────────────────────┐ ┌───────────────────────────┐
│ infrastructure/database/     │ │ infrastructure/database/  │
│ sqlite/ (sqlite_adapter.c)   │ │ postgres/ (postgres_...c) │
│ - WAL / busy_timeout         │ │ - $1/$2 param translation │
│ - SAVEPOINT name             │ │ - SAVEPOINT TO syntax     │
│ - sqlite3 / csilk_db bridge  │ │ - Retry with backoff      │
└──────────────────────────────┘ └───────────────────────────┘
```

---

## 3. 核心抽象设计 (`infrastructure/database/`)

### 3.1 数据库与连接管理 (`database.h`)

#### 类型与枚举
```c
typedef enum {
    MF_DB_ENGINE_SQLITE = 0,
    MF_DB_ENGINE_POSTGRES = 1
} mf_db_engine_t;

typedef struct {
    mf_db_engine_t engine;
    const char*    dsn;
    int            min_connections;
    int            max_connections;
    int            idle_timeout_ms;
    int            busy_timeout_ms;
    int            max_retries;
    int            retry_interval_ms;
} mf_db_config_t;

typedef struct mf_db_s mf_db_t;
```

#### 生命周期与连接池接口
- `int mf_db_open(const mf_db_config_t* config, mf_db_t** out_db);`
  根据配置创建并初始化连接池，对 SQLite 自动执行 `PRAGMA journal_mode=WAL` 与 `busy_timeout`；对 PostgreSQL 建立连接通道。
- `void mf_db_close(mf_db_t* db);`
  释放连接池中所有活跃连接，析构互斥资源。
- `mf_db_engine_t mf_db_get_engine(const mf_db_t* db);`
  获取当前连接池对应的引擎类型。
- `int mf_db_execute(mf_db_t* db, const char* sql);`
  在连接池空闲连接上执行非查询语句。
- `int mf_db_execute_with_retry(mf_db_t* db, const char* sql);`
  针对瞬态锁冲突（如 SQLite BUSY、PG 连接抖动）执行带指数退避的自动重试。

### 3.2 事务与保存点接口 (`transaction.h`)

#### 类型定义
```c
typedef struct mf_tx_s mf_tx_t;
```

#### 接口定义
- `int mf_tx_begin(mf_db_t* db, mf_tx_t** out_tx);`
  从连接池中签出专用物理连接，启动底层数据库事务（`BEGIN TRANSACTION`）。
- `int mf_tx_commit(mf_tx_t* tx);`
  提交事务并将连接归还至连接池。
- `int mf_tx_rollback(mf_tx_t* tx);`
  回滚事务并将连接归还至连接池。
- `int mf_tx_savepoint(mf_tx_t* tx, const char* name);`
  在事务中设立保存点。适配器负责生成底层方言指令（如 `SAVEPOINT name;`）。
- `int mf_tx_rollback_to_savepoint(mf_tx_t* tx, const char* name);`
  局部回滚至指定保存点。SQLite 自动适配 `ROLLBACK TO <name>`，PostgreSQL 自动适配 `ROLLBACK TO SAVEPOINT <name>`。
- `int mf_tx_release_savepoint(mf_tx_t* tx, const char* name);`
  释放保存点。
- `int mf_tx_execute(mf_tx_t* tx, const char* sql);`
  在事务当前绑定的专用连接中执行 SQL。
- `int mf_tx_prepare(mf_tx_t* tx, const char* sql, struct mf_stmt_s** out_stmt);`
  在事务专属连接中创建预编译语句句柄。

### 3.3 预编译语句与参数绑定 (`statement.h`)

#### 类型与游标
```c
typedef struct mf_stmt_s mf_stmt_t;
typedef struct mf_result_s mf_result_t;
```

#### 语句接口
- `int mf_stmt_prepare(mf_db_t* db, const char* sql, mf_stmt_t** out_stmt);`
- `void mf_stmt_close(mf_stmt_t* stmt);`
- `void mf_stmt_reset(mf_stmt_t* stmt);`
- **强类型参数绑定 (1-indexed)**：
  - `int mf_stmt_bind_int64(mf_stmt_t* stmt, int idx, int64_t val);`
  - `int mf_stmt_bind_double(mf_stmt_t* stmt, int idx, double val);`
  - `int mf_stmt_bind_text(mf_stmt_t* stmt, int idx, const char* text);`
  - `int mf_stmt_bind_bool(mf_stmt_t* stmt, int idx, bool val);`
  - `int mf_stmt_bind_null(mf_stmt_t* stmt, int idx);`
- **执行与查询**：
  - `int mf_stmt_execute(mf_stmt_t* stmt, int64_t* out_affected_rows);`
  - `int mf_stmt_query(mf_stmt_t* stmt, mf_result_t** out_result);`

#### 结果集游标接口
- `bool mf_result_next(mf_result_t* res);`
- `int64_t mf_result_get_int64(mf_result_t* res, const char* col_name);`
- `double mf_result_get_double(mf_result_t* res, const char* col_name);`
- `const char* mf_result_get_text(mf_result_t* res, const char* col_name);`
- `bool mf_result_get_bool(mf_result_t* res, const char* col_name);`
- `bool mf_result_is_null(mf_result_t* res, const char* col_name);`
- `void mf_result_free(mf_result_t* res);`
- `csilk_json_t* mf_result_to_json(mf_result_t* res);` (无缝兼容现有 Controller)

---

## 4. 适配器实现与引擎差异消除

### 4.1 SQLite 适配器 (`infrastructure/database/sqlite/`)
- 封装 SQLite 特性：
  - 默认开启 WAL 模式 (`PRAGMA journal_mode=WAL;`) 与忙时等待 (`PRAGMA busy_timeout=5000;`)。
  - 参数占位符直接使用原生 `?`。
  - 保存点语法：`SAVEPOINT <name>`, `ROLLBACK TO <name>`, `RELEASE <name>`。

### 4.2 PostgreSQL 适配器 (`infrastructure/database/postgres/`)
- 封装 PostgreSQL 特性：
  - 占位符透明转换：Repository 统一书写带有 `?` 的 SQL，PostgreSQL 适配器在语句编译阶段自动将非字面量 `?` 依序映射为 `$1, $2, ...`。
  - 保存点语法：`SAVEPOINT <name>`, `ROLLBACK TO SAVEPOINT <name>`, `RELEASE SAVEPOINT <name>`。
  - 故障重试：针对网络瞬态断开执行指数退避重连。

### 4.3 彻底消除 `if (database_type == ...)`
- 在 Repository 源码中，严禁出现 `if (database_type == ...)` 或 `if (db_is_postgres())`。
- 对于 `UPSERT` 历史净值等操作（原 `price_history_repo.c`），使用统一兼容的 `INSERT INTO ... ON CONFLICT(asset_id, price_date) DO UPDATE SET price=excluded.price, currency=excluded.currency`，由适配器在语法解析层提供大小写与 CAST 规整，保证两端透明执行。

---

## 5. Repository 边界规范与重构清单

### 5.1 五大禁止落地
1. **禁止业务规则**：任何限额判断、状态机转移校验均移至 Domain / Application 层。
2. **禁止 HTTP**：参数全部为原生 C 类型或 Domain 实体指针，返回值全部为状态码或实体结果，禁止引入 `csilk_ctx_t*`。
3. **禁止 AI workflow**：Prompt 模板、LLM 分发禁止出现在 Repository 中。
4. **禁止权限决策**：RBAC（Owner/Editor/Viewer）判断由鉴权中间件与服务层处理，Repository 仅以参数中的 `user_id` / `tenant_id` 执行数据物理隔离。
5. **禁止业务计算**：负债符号翻转、成本基础/未实现盈亏推导统一调用 `core/financial` 与 `core/ledger`。

### 5.2 Repository 统一命名与重构目标
- `repositories/user_repository.h/.c`
- `repositories/asset_repository.h/.c`
- `repositories/transaction_repository.h/.c`
- `repositories/portfolio_repository.h/.c`
- `repositories/price_history_repository.h/.c`

为保证项目平滑升级，保留向后兼容桥接函数（如 `asset_list`, `tx_get` 等旧接口），内部转调新适配层，确保既有 139 项集成测试断言 100% 通过。

---

## 6. 测试与质量保证

### 6.1 单元测试：双引擎行为一致性 (`tests/unit/test_database_repository.c`)
- 定义统一的测试驱动宏 `RUN_TEST_FOR_ENGINE(engine)`：
  1. **基本 CRUD**：User/Asset 增删改查。
  2. **事务完整性**：`BEGIN` -> 写入 -> `ROLLBACK` 校验未落库；`BEGIN` -> 写入 -> `COMMIT` 校验落库。
  3. **保存点局部回滚**：
     - `BEGIN`
     - 写入 Record 1
     - `SAVEPOINT sp1`
     - 写入 Record 2
     - `ROLLBACK TO SAVEPOINT sp1`
     - `COMMIT`
     - 校验：Record 1 存在，Record 2 不存在。
  4. **强类型参数与 NULL 绑定**：验证文本、浮点数、整数、布尔及 NULL 正确存储与读取。
- **环境适配**：
  - SQLite 引擎在内存数据库中执行。
  - PostgreSQL 引擎：若配置了 `MINEFOLIO_TEST_PG_DSN` 则连接真实服务执行；若未配置则运行基于适配器语法与模拟契约层的一致性验证套件，确保开发机与 CI 均可 100% 自动通过。

### 6.2 全量集成回归测试
- 24 项 CTest 单元测试必须保持 100% PASS。
- 7 大集成测试套件（`test_link.sh`, `test_ledgers.sh`, `test_2fa.sh`, `test_dca_cashflow.sh`, `test_ai_trace.sh`, `test_market_sync.sh`, `test_fx_oauth.sh`）必须保持 100% PASS。
- 前端 Vitest 与生产构建零错误。
