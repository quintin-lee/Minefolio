# SQLite 外键强制执行与引用完整性加固 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 SQLite 下安全且严格地开启外键强制执行（`PRAGMA foreign_keys = ON;`），消除 SQLite 与 PostgreSQL 之间的引用完整性行为分歧；修复历史仓储层与业务层中 `0` 与 `"NULL"` 的伪外键隐患，修复测试夹具中的幽灵用户/分类问题，确保后端 32 个 CTest 单元测试与全部 8 个端到端集成测试 100% 绿色通过。

**Architecture:** 
1. **仓储与业务层参数语义收敛**：在 `transaction_repo_impl.c` 中将 `category_id` 与 `linked_asset_id` 的入参绑定对齐为 `CAST(NULLIF(?, '0') AS BIGINT)`，彻底杜绝字符串 `"NULL"` 或 `0` 被类型亲和转换为非法外键 ID 的隐患；在 `cashflow/usecases.c` 中规范分类关联。
2. **测试夹具契约对齐**：修复 `test_ledger_engine.c` 中 `users` 表字段名拼写错误（`password_hash` -> `password`），杜绝因幽灵用户导致的外键级联失败；修复 `test_domain_repositories.c` 中创建资产前缺失关联有效分类的问题。
3. **安全启用运行时外键**：在 `backend/src/common/db.c` 中为 SQLite 连接池启用 `PRAGMA foreign_keys = ON;`。
4. **全量端到端验证**：针对性验证现金流收益确认（`test_dca_cashflow.sh`）与非法资产负向拦截原子回滚（`test_link.sh` §10），保证 32 个 CTest 与全部集成测试 100% PASS。

**Tech Stack:** C23, SQLite 3, PostgreSQL 16 兼容, csilk 0.5.3, Bash 自动化集成测试套件。

---

## File Structure

- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:450-485`
  - 职责：修复 `tx_insert` 参数绑定，使用 `CAST(NULLIF(?, '0') AS BIGINT)` 处理 `linked_asset_id` 与 `category_id`，杜绝将字面量 `"NULL"` 作为数字 0 存入外键列。
- Modify: `backend/src/application/cashflow/usecases.c:415-440`
  - 职责：现金流收入确认时正确处理分类与关联资产，避免传递非法 0 值外键。
- Modify: `backend/tests/unit/test_ledger_engine.c:55-75`
  - 职责：修复种子用户插入 SQL 字段名（`password_hash` 改为 `password`），建立有效的前置外键父实体。
- Modify: `backend/tests/unit/test_domain_repositories.c:148-175`
  - 职责：在创建资产前关联有效的测试分类 ID，消除 `category_id` 默认为 0 的外键冲突。
- Modify: `backend/src/common/db.c:83-93`
  - 职责：在 `db_init` 的 SQLite 初始化分支中添加 `csilk_db_exec(g_pool, "PRAGMA foreign_keys=ON;");`。

---

### Task 1: 仓储层与应用层参数外键契约清洗 (Fix 0 & "NULL" Pseudo-FKs in tx_insert & cashflow)

**Files:**
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:450-485`
- Modify: `backend/src/application/cashflow/usecases.c:415-440`
- Test: `backend/tests/test_dca_cashflow.sh`

- [ ] **Step 1: 检查 `tx_insert` 的参数绑定漏洞**

在 `backend/src/infrastructure/repositories/transaction_repo_impl.c` 中，原实现为：
```c
    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions "
        "(user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,"
        "linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note,ledger_"
        "id) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, "
        "COALESCE((SELECT ledger_id FROM assets WHERE id = ?), (SELECT id FROM ledgers WHERE "
        "owner_id = ? AND is_default = 1))) RETURNING id",
        (const char*[]){uid,
                        ast,
                        linked_asset_id > 0 ? last : "NULL",
                        cat,
                        ...
```
当 `linked_asset_id <= 0` 时传入字符串 `"NULL"`，SQLite 的整数类型亲和性将其转换为整数 `0`，而外键列 `linked_asset_id` 不存在 id 为 0 的资产，导致约束失败。同理，当 `category_id <= 0` 时传入 `"0"` 也会触发 `category_id` 外键约束失败。

- [ ] **Step 2: 修改 `tx_insert` 采用与 `ledger_engine.c` 一致的安全绑定**

在 `backend/src/infrastructure/repositories/transaction_repo_impl.c` 中：
```c
    char last_str[32], cat_str[32];
    if (linked_asset_id > 0) {
        snprintf(last_str, sizeof(last_str), "%lld", (long long)linked_asset_id);
    } else {
        last_str[0] = '\0';
    }
    if (category_id > 0) {
        snprintf(cat_str, sizeof(cat_str), "%lld", (long long)category_id);
    } else {
        cat_str[0] = '\0';
    }

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transactions "
        "(user_id,asset_id,linked_asset_id,category_id,source_type,transaction_type,direction,"
        "linked_direction,amount,price_per_unit,quantity,fee,currency,transaction_date,note,ledger_"
        "id) "
        "VALUES (?,?,CAST(NULLIF(?, '') AS BIGINT),CAST(NULLIF(?, '') AS BIGINT),?,?,?, "
        "NULLIF(?, ''),?,?,?,?,?,?,?, "
        "COALESCE((SELECT ledger_id FROM assets WHERE id = ?), (SELECT id FROM ledgers WHERE "
        "owner_id = ? AND is_default = 1))) RETURNING id",
        (const char*[]){uid,
                        ast,
                        last_str,
                        cat_str,
                        source_type,
                        transaction_type,
                        direction ? "in" : "out",
                        (linked_direction && linked_direction[0]) ? linked_direction : "",
                        amt,
                        pp,
                        qty,
                        fee_s,
                        currency,
                        date,
                        note ? note : "",
                        ast,
                        uid,
                        NULL});
```

- [ ] **Step 3: 检查并规范 `cashflow/usecases.c` 中的入账调用**

检查 `backend/src/application/cashflow/usecases.c:418-433`，确认 `tx_insert` 传入的 `category_id = 0` 在 `CAST(NULLIF(?, '') AS BIGINT)` 作用下被安全处理为 SQL `NULL`。

- [ ] **Step 4: 编译验证**

```bash
cmake --build backend/build --parallel
```
预期：编译 0 错误。

- [ ] **Step 5: 提交变更**

```bash
git add backend/src/infrastructure/repositories/transaction_repo_impl.c backend/src/application/cashflow/usecases.c
git commit -m "fix(transaction): 🐛 sanitize nullable foreign keys in tx_insert to sql null"
```

---

### Task 2: 单元测试夹具幽灵外键修复 (Fix Phantom Foreign Keys in Unit Tests)

**Files:**
- Modify: `backend/tests/unit/test_ledger_engine.c:55-75`
- Modify: `backend/tests/unit/test_domain_repositories.c:148-175`
- Test: `ctest --test-dir backend/build -R "test_ledger_engine|test_domain_repositories"`

- [ ] **Step 1: 修复 `test_ledger_engine.c` 的种子数据列名**

在 `backend/tests/unit/test_ledger_engine.c:60`：
将错误的字段名 `password_hash` 改为真实 schema 字段名 `password`：
```c
    // 1. Seed User & Categories & Assets
    int64_t user_id = 1;
    csilk_db_exec(pool, "INSERT INTO users (id, username, password) VALUES (1, 'tester', 'hash123')");
```

- [ ] **Step 2: 修复 `test_domain_repositories.c` 资产测试前置分类**

在 `backend/tests/unit/test_domain_repositories.c` 的 `test_asset_repository` 中：
```c
static void
test_asset_repository(csilk_db_pool_t* pool, int64_t user_id, int64_t* out_asset_id)
{
    printf("--- 2. Testing Asset Domain Repository ---\n");

    // Ensure a valid category exists for foreign key constraint
    csilk_db_exec(pool, "INSERT OR IGNORE INTO categories (id, user_id, name, type, asset_type) "
                        "VALUES (1, 1, 'Default Stock', 'expense', 'stock')");

    mf_asset_t asset;
    memset(&asset, 0, sizeof(asset));
    asset.user_id = user_id;
    asset.category_id = 1;
    snprintf(asset.name, sizeof(asset.name), "Test Asset");
    snprintf(asset.asset_type, sizeof(asset.asset_type), "stock");
    asset.quantity = test_qty(100.0);
    asset.cost_basis = test_money(10000.0);
    asset.net_value = test_price(1800.0);
    asset.currency = test_cny();
```

- [ ] **Step 3: 编译并运行单元测试验证**

```bash
cmake --build backend/build --parallel && ctest --test-dir backend/build -R "test_ledger_engine|test_domain_repositories" --output-on-failure
```
预期：PASS=2 FAIL=0。

- [ ] **Step 4: 提交变更**

```bash
git add backend/tests/unit/test_ledger_engine.c backend/tests/unit/test_domain_repositories.c
git commit -m "test(core): 🧪 align test fixtures with schema foreign key constraints"
```

---

### Task 3: 启用 SQLite 运行时外键约束 (Enable SQLite Foreign Keys Enforcement)

**Files:**
- Modify: `backend/src/common/db.c:83-93`
- Test: `ctest --test-dir backend/build`

- [ ] **Step 1: 在 `backend/src/common/db.c` 中开启 `PRAGMA foreign_keys = ON;`**

在 `db_init` 的 `if (!g_is_postgres)` 块内添加执行外键强制使能：
```c
    if (!g_is_postgres) {
        csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
        csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
        csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
        csilk_db_exec(g_pool, "PRAGMA foreign_keys=ON;");
        csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
        csilk_db_exec(g_pool,
                      "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");
    }
```

- [ ] **Step 2: 编译全量后端**

```bash
cmake --build backend/build --parallel
```
预期：0 错误 0 告警。

- [ ] **Step 3: 运行全部 32 个 CTest 单元测试套件**

```bash
ctest --test-dir backend/build --output-on-failure
```
预期输出：`100% tests passed out of 32`。

- [ ] **Step 4: 提交变更**

```bash
git add backend/src/common/db.c
git commit -m "fix(db): 🎯 enforce sqlite foreign keys to match postgres behavior"
```

---

### Task 4: 端到端集成测试与负向外键探针全量验证 (Full Integration Suites & FK Verification)

**Files:**
- Test: `backend/tests/test_dca_cashflow.sh`
- Test: `backend/tests/test_link.sh`
- Test: `backend/tests/test_ledgers.sh`
- Test: `backend/tests/test_2fa.sh`
- Test: `backend/tests/test_ai_trace.sh`
- Test: `backend/tests/test_market_sync.sh`
- Test: `backend/tests/test_fx_oauth.sh`

- [ ] **Step 1: 验证现金流与定投集成测试 (`test_dca_cashflow.sh`)**

```bash
cd backend && ./tests/test_dca_cashflow.sh && cd ..
```
预期输出：`Bank balance after dividend income: 9200 (expected 9200)`，全部执行通过退出码 0。

- [ ] **Step 2: 验证主集成测试与非法资产负向外键拦截 (`test_link.sh`)**

```bash
cd backend && ./tests/test_link.sh && cd ..
```
预期输出：
- `== 10. 非法资产 → code 1002 且原子回滚 ==` 验证通过
- 最终结果：`PASS=151 FAIL=0`

- [ ] **Step 3: 验证其余集成测试**

```bash
cd backend
./tests/test_ledgers.sh
./tests/test_2fa.sh
./tests/test_ai_trace.sh
./tests/test_market_sync.sh
./tests/test_fx_oauth.sh
cd ..
```
预期输出：全部套件 SUCCESS。

- [ ] **Step 4: 验证前端测试与双端构建**

```bash
npm --prefix frontend test
npm --prefix frontend run build && npm --prefix frontend run build:mobile
```
预期输出：Vitest 17 个文件 78 项测试全绿，桌面端与移动端构建均 0 错误。
