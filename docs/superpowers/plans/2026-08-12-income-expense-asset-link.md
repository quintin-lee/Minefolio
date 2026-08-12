# 收支与资产动态关联 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 记账（日常收支 + 资产交易）自动增减关联资产余额，负债方向反转，所有变动写入审计日志，事务保证原子性。

**Architecture:** 方案 A——后端显式联动。新建 `balance.c` 共享模块提供 `balance_apply_delta()`（内部完成归属校验、负债方向归一化、余额原子更新、审计日志写入），`daily_expenses.c` 与 `transactions.c` 的 create/update/delete 显式调用并用 `BEGIN TRANSACTION`/`COMMIT`/`ROLLBACK` 包裹。迁移采用列存在性门控：存量库首启清空旧数据 + ALTER 加列，索引门控后无条件创建。前端收支表单加必选资产选择器。

**Tech Stack:** 后端 csilk C23 + SQLite（csilk_db_exec 直接执行 SQL 事务语句）；前端 Vue 3 + Element Plus。

**Spec:** `docs/superpowers/specs/2026-08-12-income-expense-asset-link-design.md`

---

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `backend/sql/migration.sql` | 修改 | daily_expenses 建表加 asset_id 列；新建 asset_balance_logs 表（不含 idx_daily_expenses_asset——见 Chunk 1 说明） |
| `backend/src/common/db.c` | 修改 | db_run_migrations 加列存在性门控 + 无条件索引步骤 |
| `backend/src/common/balance.h` | 新建 | balance_apply_delta / balance_direction 声明 |
| `backend/src/common/balance.c` | 新建 | 核心联动逻辑（CMake GLOB 自动包含） |
| `backend/src/daily_expenses.c` | 修改 | list 加 asset_name；create/update/delete 事务 + 联动 |
| `backend/src/transactions.c` | 修改 | create/update/delete 事务 + 联动（type_delta 映射） |
| `backend/src/assets.c` | 修改 | assets_delete 检查 csilk_db_exec 返回值 |
| `frontend/src/types/index.ts` | 修改 | DailyExpense 加 asset_id / asset_name |
| `frontend/src/views/DailyExpenses.vue` | 修改 | 表单加资产选择器（必选）、列表加关联资产列 |
| `frontend/src/views/Transactions.vue` | 修改 | 编辑对话框加"修改金额将同步调整资产余额"提示（spec §8.3） |
| `backend/tests/test_link.sh` | 新建 | curl + sqlite3 集成测试脚本 |

**测试策略（重要背景）**：项目当前**无任何测试基础设施**（backend 无 tests/、无 pytest/gtest；frontend 无 vitest）。本计划不引入新框架（YAGNI、符合项目现状），采用**真实服务器 + curl + sqlite3 CLI 的集成测试脚本** `backend/tests/test_link.sh`：启动带独立临时 DB 的服务器，通过 HTTP API 操作，用 sqlite3 直接查询数据库验证余额与审计日志。覆盖 spec §9.1/9.2/9.3 全部核心场景。

**构建/运行约定（已实测）**：
- 构建：`cd backend/build && cmake .. && make`（CMake `file(GLOB src/*.c src/common/*.c)` 自动包含新文件 balance.c，无需改 CMakeLists）。注意：GLOB 不追踪新文件，若增量构建未重新扫描需删 `build/CMakeFiles` 内 cmake.check_cache 或 touch CMakeLists.txt。
- 运行：`cd backend/build && MINEFOLIO_DB_DSN=/tmp/xxx.db ./minefolio`（sql/ 与 config/ 已由 POST_BUILD 复制到 build 目录）。默认端口 8080。
- 前端：`cd frontend && npm run dev`（vite 代理 /api → localhost:8080）。
- 环境已有：sqlite3、curl、jq 均可直接使用。

---

## Chunk 1: 数据库迁移（migration.sql + db.c 门控）

### Task 1.1: 更新 migration.sql（全新库 schema）

**Files:**
- Modify: `backend/sql/migration.sql:87-98`（daily_expenses 建表）

**背景（已实测确认）**：`migration.sql` 用 `CREATE TABLE IF NOT EXISTS` 幂等建表，`db_run_migrations()` 每次启动整段执行（csilk_db_exec 用 sqlite3_exec 批处理，首个语句报错即中止整批）。`idx_daily_expenses_asset` **不能**放进 migration.sql——存量库首启时 asset_id 列尚不存在，会报 `no such column: asset_id` 中止整批（IF NOT EXISTS 只防索引已存在，不抑制此错）。该索引由 db.c 第 3 步无条件创建（Task 1.2）。

- [ ] **Step 1: 修改 daily_expenses 建表语句，加入 asset_id 列**

将现有（L87-98）：
```sql
CREATE TABLE IF NOT EXISTS daily_expenses (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id  INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    expense_type TEXT NOT NULL CHECK(expense_type IN ('expense', 'income')),
    amount       DECIMAL(18,2) NOT NULL,
    currency     TEXT DEFAULT 'CNY',
    expense_date DATE NOT NULL,
    note         TEXT,
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```
改为（新增 `asset_id` 列，放在 category_id 之后）：
```sql
CREATE TABLE IF NOT EXISTS daily_expenses (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id  INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    asset_id     INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    expense_type TEXT NOT NULL CHECK(expense_type IN ('expense', 'income')),
    amount       DECIMAL(18,2) NOT NULL,
    currency     TEXT DEFAULT 'CNY',
    expense_date DATE NOT NULL,
    note         TEXT,
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

- [ ] **Step 2: 追加 asset_balance_logs 建表语句（文件末尾）**

在 migration.sql 末尾（L104 之后）追加：
```sql
-- 资产余额审计日志（只增不删；asset_id 不设外键——删资产后日志保留）
CREATE TABLE IF NOT EXISTS asset_balance_logs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id      INTEGER NOT NULL,
    user_id       INTEGER NOT NULL REFERENCES users(id),
    delta         DECIMAL(18,2) NOT NULL,
    balance_after DECIMAL(18,2) NOT NULL,
    source_type   TEXT NOT NULL,
    source_id     INTEGER NOT NULL,
    note          TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_balance_logs_asset ON asset_balance_logs(asset_id, created_at);
```

> **注意**：migration.sql 中**不加** `CREATE INDEX idx_daily_expenses_asset`——由 Task 1.2 第 3 步无条件执行。asset_balance_logs 的索引 `idx_balance_logs_asset` 可放此处（该表列在新建时已齐全，无"列尚不存在"问题）。

- [ ] **Step 3: 验证 migration.sql 在全新库上执行成功**

```bash
rm -f /tmp/mf_mig_fresh.db && sqlite3 /tmp/mf_mig_fresh.db < backend/sql/migration.sql && sqlite3 /tmp/mf_mig_fresh.db ".schema daily_expenses" | grep asset_id && sqlite3 /tmp/mf_mig_fresh.db ".tables"
```
Expected: `.schema daily_expenses` 输出含 `asset_id` 行；`.tables` 含 `asset_balance_logs`。

### Task 1.2: db.c 门控迁移 + 无条件索引

**Files:**
- Modify: `backend/src/common/db.c`（db_run_migrations 函数，当前 L24-56；末尾 `free(sql); return 0;` 在 L53-55）

**背景（已实测）**：现有 `db_run_migrations` 读 migration.sql 整段执行后，再单独执行一条"忽略失败"的 categories ALTER。`csilk_db_query_json(pool, "PRAGMA table_info(daily_expenses)")` 可查列（返回每列 `name` 字段）。

- [ ] **Step 1: 修改 db_run_migrations，在 migration.sql 执行后加入门控逻辑**

**精确替换说明**：将 db_run_migrations 函数体从 `// Try adding 'type' column...` 注释开始（当前 L51）到函数末尾 `return 0;`（当前 L55）之间的内容**整体替换**为下面的完整代码块（含门控逻辑 + 无条件索引 + 末尾 `free(sql); return 0;`）。**注意**：categories 的 ALTER 语句在替换块中出现一次即可，不要重复；`free(sql); return 0;` 必须保留在函数末尾（删除会导致内存泄漏 + int 函数无返回值的编译错误）。

```c
    // Try adding 'type' column for pre-existing databases (ignore failure if column already exists)
    csilk_db_exec(pool, "ALTER TABLE categories ADD COLUMN type TEXT NOT NULL DEFAULT 'asset'");

    // ---- 收支-资产联动迁移（列存在性门控，一次性） ----
    // 检测 daily_expenses 是否已有 asset_id 列
    int has_asset_id = 0;
    csilk_json_t* cols = csilk_db_query_json(pool, "PRAGMA table_info(daily_expenses)");
    if (cols) {
        size_t n = csilk_json_array_size(cols);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* col = csilk_json_array_get(cols, i);
            const char* cname = csilk_json_get_string(col, "name");
            if (cname && strcmp(cname, "asset_id") == 0) { has_asset_id = 1; break; }
        }
        csilk_json_free(cols);
    }

    if (!has_asset_id) {
        // 存量库一次性迁移：清空旧数据（用户已确认）+ 加列（门控闭合点）
        csilk_db_exec(pool, "DELETE FROM expense_tags");
        csilk_db_exec(pool, "DELETE FROM daily_expenses");
        csilk_db_exec(pool, "DELETE FROM transactions");
        if (csilk_db_exec(pool,
                "ALTER TABLE daily_expenses ADD COLUMN asset_id INTEGER NOT NULL "
                "REFERENCES assets(id) ON DELETE CASCADE") != 0) {
            fprintf(stderr, "Migration error: cannot add asset_id to daily_expenses\n");
            free(sql);
            return -1;
        }
    }

    // 无条件幂等建索引（全新库首启 / 存量库 ALTER 后 / 失败自愈均覆盖）
    csilk_db_exec(pool, "CREATE INDEX IF NOT EXISTS idx_daily_expenses_asset ON daily_expenses(asset_id)");

    free(sql);
    return 0;
}
```

同时确保 db.c 顶部已 include 所需头：`csilk/csilk.h`（提供 csilk_db_query_json / csilk_json_*）与 `<string.h>`。**检查现有 includes**，缺则补：
```c
#include <string.h>
#include "csilk/csilk.h"
```

- [ ] **Step 2: 构建**

```bash
cd backend/build && touch ../CMakeLists.txt && cmake .. >/dev/null && make 2>&1 | tail -5
```
Expected: 编译成功，无 error。

- [ ] **Step 3: 验证存量库迁移（清空 + ALTER + 索引）**

```bash
# 构造旧 schema 库：用 git show 的旧 migration.sql（无 asset_id）建库，插入一条脏数据
cd backend/build
git -C ../.. show HEAD:backend/sql/migration.sql > /tmp/old_migration.sql
rm -f /tmp/mf_old.db
sqlite3 /tmp/mf_old.db < /tmp/old_migration.sql
sqlite3 /tmp/mf_old.db "INSERT INTO daily_expenses (user_id, category_id, expense_type, amount, currency, expense_date) VALUES (1, 1, 'expense', 100, 'CNY', '2026-08-01');"
MINEFOLIO_DB_DSN=/tmp/mf_old.db timeout 2 ./minefolio & sleep 1; kill %1 2>/dev/null
sqlite3 /tmp/mf_old.db ".schema daily_expenses" | grep asset_id
echo "rows_after_migration:"; sqlite3 /tmp/mf_old.db "SELECT COUNT(*) FROM daily_expenses;"
echo "index:"; sqlite3 /tmp/mf_old.db "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_daily_expenses_asset';"
echo "audit_table:"; sqlite3 /tmp/mf_old.db "SELECT name FROM sqlite_master WHERE type='table' AND name='asset_balance_logs';"
```
Expected: asset_id 列存在；`rows_after_migration: 0`（旧数据被清空）；index 与 audit_table 均输出名称。

- [ ] **Step 4: 验证存量库第二次启动（门控不触发，数据保留）**

```bash
cd backend/build
sqlite3 /tmp/mf_old.db "INSERT INTO daily_expenses (user_id, category_id, asset_id, expense_type, amount, currency, expense_date) VALUES (1, 1, 1, 'expense', 100, 'CNY', '2026-08-01');"
MINEFOLIO_DB_DSN=/tmp/mf_old.db timeout 2 ./minefolio & sleep 1; kill %1 2>/dev/null
echo "rows_after_second_boot:"; sqlite3 /tmp/mf_old.db "SELECT COUNT(*) FROM daily_expenses;"
```
Expected: `rows_after_second_boot: 1`（数据保留，未重复清空）。

- [ ] **Step 5: 验证全新库启动（索引存在）**

```bash
cd backend/build
rm -f /tmp/mf_fresh.db
MINEFOLIO_DB_DSN=/tmp/mf_fresh.db timeout 2 ./minefolio & sleep 1; kill %1 2>/dev/null
sqlite3 /tmp/mf_fresh.db "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_daily_expenses_asset';"
sqlite3 /tmp/mf_fresh.db ".schema asset_balance_logs" | head -2
```
Expected: 索引名输出；asset_balance_logs schema 输出。

- [ ] **Step 6: Commit**

```bash
git add backend/sql/migration.sql backend/src/common/db.c
git commit -m "feat(db): 收支-资产联动迁移——asset_id 列门控迁移 + 审计日志表"
```

---

## Chunk 2: balance 共享模块

### Task 2.1: 新建 balance.h / balance.c

**Files:**
- Create: `backend/src/common/balance.h`
- Create: `backend/src/common/balance.c`

- [ ] **Step 1: 写 balance.h**

```c
#pragma once
#include "csilk/drivers/db.h"
#include <stdint.h>

/**
 * @brief 对资产余额应用增减，并写入审计日志。
 *
 * delta 为业务方向金额（收入/入金为正，支出/出金为负），函数内部根据资产
 * 类型（负债方向反转）归一化后更新 current_value，并记录 balance_after 快照。
 *
 * @param pool        数据库连接池
 * @param asset_id    目标资产 id
 * @param user_id     操作者（审计 + 归属校验）
 * @param delta       业务方向金额（正=增加余额，负=减少余额）
 * @param source_type "daily_expense" 或 "transaction"
 * @param source_id   对应主记录 id
 * @param note        冗余描述（可为 NULL）
 * @return 0 成功；-1 资产不存在或不属于该用户；-2 数据库错误
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t asset_id, int64_t user_id, double delta,
                        const char* source_type, int64_t source_id,
                        const char* note);

/** @brief 判断资产类型是否为负债（方向反转）。1=普通资产，-1=负债。 */
int balance_direction(const char* asset_type);
```

- [ ] **Step 2: 写 balance.c**

```c
#include "common/balance.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <string.h>

int balance_direction(const char* asset_type) {
    if (!asset_type) return 1;
    if (strcmp(asset_type, "loan") == 0 ||
        strcmp(asset_type, "credit_card") == 0 ||
        strcmp(asset_type, "other_liability") == 0) {
        return -1;
    }
    return 1;
}

int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t asset_id, int64_t user_id, double delta,
                        const char* source_type, int64_t source_id,
                        const char* note) {
    if (!pool || asset_id <= 0 || user_id <= 0 || !source_type) return -1;

    char sql[512];

    // 1. 查询资产归属与类型（asset_type 存于 categories，必须 JOIN）
    snprintf(sql, sizeof(sql),
        "SELECT a.current_value, c.asset_type "
        "FROM assets a JOIN categories c ON a.category_id = c.id "
        "WHERE a.id=%lld AND a.user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* row = csilk_db_query_json(pool, sql);
    if (!row) return -2;
    if (csilk_json_array_size(row) == 0) {
        csilk_json_free(row);
        return -1;  // 资产不存在或不属于该用户
    }
    const csilk_json_t* asset = csilk_json_array_get(row, 0);
    const char* asset_type = csilk_json_get_string(asset, "asset_type");

    // 2. 归一化 delta（负债方向反转）
    double signed_delta = delta * balance_direction(asset_type);
    csilk_json_free(row);

    // 3. 原子更新余额（避免读改写竞态）
    snprintf(sql, sizeof(sql),
        "UPDATE assets SET current_value = current_value + %.2f, "
        "updated_at = CURRENT_TIMESTAMP WHERE id=%lld AND user_id=%lld",
        signed_delta, (long long)asset_id, (long long)user_id);
    if (csilk_db_exec(pool, sql) != 0) return -2;

    // 4. 读取变动后余额（balance_after 快照）
    snprintf(sql, sizeof(sql),
        "SELECT current_value FROM assets WHERE id=%lld AND user_id=%lld",
        (long long)asset_id, (long long)user_id);
    csilk_json_t* after = csilk_db_query_json(pool, sql);
    if (!after || csilk_json_array_size(after) == 0) {
        if (after) csilk_json_free(after);
        return -2;
    }
    double balance_after = db_get_num(csilk_json_array_get(after, 0), "current_value");
    csilk_json_free(after);

    // 5. 写审计日志（delta 存已反转的 signed_delta）
    snprintf(sql, sizeof(sql),
        "INSERT INTO asset_balance_logs (asset_id, user_id, delta, balance_after, "
        "source_type, source_id, note) "
        "VALUES (%lld, %lld, %.2f, %.2f, '%s', %lld, '%s')",
        (long long)asset_id, (long long)user_id, signed_delta, balance_after,
        source_type, (long long)source_id, note ? note : "");
    if (csilk_db_exec(pool, sql) != 0) return -2;

    return 0;
}
```

> 安全说明：`source_type` 恒为内部常量；`note` 来自用户输入但与本项目其余 handler 的 snprintf 拼接风格一致（主记录插入同样不转义）。`sql[512]` 容量与现有代码一致。

- [ ] **Step 3: 构建验证**

```bash
cd backend/build && touch ../CMakeLists.txt && cmake .. >/dev/null && make 2>&1 | tail -5
```
Expected: 编译成功（GLOB 自动包含 balance.c），无 error、无 warning。

- [ ] **Step 4: Commit**

```bash
git add backend/src/common/balance.h backend/src/common/balance.c
git commit -m "feat(balance): 新增 balance_apply_delta 联动模块——归属校验+负债反转+审计日志"
```

---

## Chunk 3: daily_expenses.c 改造

**Files:**
- Modify: `backend/src/daily_expenses.c`

### Task 3.1: list 接口加 asset_id / asset_name

**背景（已实测）**：daily_expenses_list 的 SELECT 手工拼 SQL，结果用手工逐列重建 JSON 行（L58-78）——遗漏字段会导致前端拿不到。需在 SELECT 加 `de.asset_id` 与 `a.name as asset_name`（JOIN assets），并在重建循环中显式添加两个字段。

- [ ] **Step 1: 修改 SELECT 语句（L20-29，含 `char sql[1024];` 声明行）**

> ⚠️ **替换范围含声明行**：`char sql[1024];` 在 L20，`snprintf(...)` 至 L29。整体替换 L20-29，**不要**从 L21 开始（否则会留下重复的 `char sql[1024];` 声明，编译报错）。

原：
```c
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT de.id, de.user_id, de.category_id, de.expense_type, de.amount, "
        "de.currency, de.expense_date, de.note, de.created_at, de.updated_at, "
        "c.name as category_name, "
        "(SELECT json_group_array(json_object('id', t.id, 'name', t.name, 'color', t.color)) "
        " FROM expense_tags et JOIN tags t ON et.tag_id=t.id "
        " WHERE et.expense_id=de.id) as tags "
        "FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id "
        "WHERE de.user_id=%lld", (long long)user_id);
```
改为（加 `de.asset_id`、`a.name as asset_name`、`LEFT JOIN assets a`）：
```c
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT de.id, de.user_id, de.category_id, de.asset_id, de.expense_type, de.amount, "
        "de.currency, de.expense_date, de.note, de.created_at, de.updated_at, "
        "c.name as category_name, a.name as asset_name, "
        "(SELECT json_group_array(json_object('id', t.id, 'name', t.name, 'color', t.color)) "
        " FROM expense_tags et JOIN tags t ON et.tag_id=t.id "
        " WHERE et.expense_id=de.id) as tags "
        "FROM daily_expenses de "
        "LEFT JOIN categories c ON de.category_id=c.id "
        "LEFT JOIN assets a ON de.asset_id=a.id "
        "WHERE de.user_id=%lld", (long long)user_id);
```

- [ ] **Step 2: 在手工行重建循环中加 asset_id / asset_name（L63-73 附近）**

在 `csilk_json_add_string(item, "category_name", ...)` 前后插入：
```c
        csilk_json_add_number(item, "asset_id", db_get_num(row, "asset_id"));
        csilk_json_add_string(item, "asset_name", csilk_json_get_string(row, "asset_name"));
```

### Task 3.2: create 事务 + 联动

**背景（已实测）**：create 当前无事务（L106-141）。需：校验 asset_id 必填 → BEGIN → INSERT（含 asset_id）→ balance_apply_delta → tags → COMMIT / 失败 ROLLBACK。

- [ ] **Step 1: 校验部分加 asset_id（L90-99 附近）**

原：
```c
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");

    if (category_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "category_id、expense_type、amount、expense_date 为必填");
        return;
    }
```
改为：
```c
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    int64_t asset_id = (int64_t)csilk_json_get_number(body, "asset_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");

    if (category_id <= 0 || asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        respond_bad_request(c, "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return;
    }
```

- [ ] **Step 2: INSERT 语句加 asset_id 列（L108-111）**

原：
```c
    snprintf(sql, sizeof(sql),
        "INSERT INTO daily_expenses (user_id, category_id, expense_type, amount, currency, expense_date, note) "
        "VALUES (%lld, %lld, '%s', %.2f, '%s', '%s', '%s') RETURNING id",
        (long long)user_id, (long long)category_id, type, amount, currency, date, note ? note : "");
```
改为：
```c
    snprintf(sql, sizeof(sql),
        "INSERT INTO daily_expenses (user_id, category_id, asset_id, expense_type, amount, currency, expense_date, note) "
        "VALUES (%lld, %lld, %lld, '%s', %.2f, '%s', '%s', '%s') RETURNING id",
        (long long)user_id, (long long)category_id, (long long)asset_id,
        type, amount, currency, date, note ? note : "");
```

- [ ] **Step 3: INSERT 后加事务 + 联动调用（L113-141 区域重写）**

将 INSERT 执行与 tags 处理包裹进事务，并在取到 expense_id 后调用 balance_apply_delta。替换 L113-141：

```c
    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* ins = csilk_db_query_json(pool, sql);
    if (!ins || csilk_json_array_size(ins) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (ins) csilk_json_free(ins);
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    int64_t expense_id = db_get_int(csilk_json_array_get(ins, 0), "id");
    csilk_json_free(ins);

    // 联动资产余额（income=+，expense=-；负债方向在 balance_apply_delta 内反转）
    double business_delta = (strcmp(type, "income") == 0) ? amount : -amount;
    if (balance_apply_delta(pool, asset_id, user_id, business_delta,
                            "daily_expense", expense_id, note) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(body);
        respond_bad_request(c, "资产无效");
        return;
    }

    // Handle tags
    if (tags && csilk_json_is_array(tags)) { // CSILK_JSON_ARRAY
        size_t n = csilk_json_array_size(tags);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tag = csilk_json_array_get(tags, i);
            int64_t tag_id = (int64_t)csilk_json_get_number(tag, "id");
            if (tag_id <= 0) continue;

            char tag_sql[256];
            snprintf(tag_sql, sizeof(tag_sql),
                "INSERT OR IGNORE INTO expense_tags (expense_id, tag_id) "
                "VALUES (%lld, %lld)",
                (long long)expense_id, (long long)tag_id);
            csilk_db_exec(pool, tag_sql);
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
```

> 说明：`csilk_db_exec(pool, "BEGIN TRANSACTION")` 返回值非 0 即事务无法开始（与 sqlite 驱动的 transaction_begin 实现等价，见探索结论）。tags 插入沿用现有忽略返回值风格（既有模式），联动调用失败才强制 ROLLBACK。同时需在文件顶部 `#include "common/balance.h"`。

### Task 3.3: update 事务 + 差量联动

**背景（已实测）**：update 当前无事务（L144-211）。需：SELECT 旧记录 → BEGIN → UPDATE 全字段（含 asset_id）→ 同资产合并差量 / 异资产回退+应用 → tags 同步 → COMMIT / 失败 ROLLBACK。

- [ ] **Step 1: 文件顶部加 include**

```c
#include "common/balance.h"
```

- [ ] **Step 2: 校验后读取旧记录（L166 之后插入）**

在 `csilk_json_free(chk);` 之后、读取 body 字段之前，插入：
```c
    // 读取旧记录（差量联动需要）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT amount, expense_type, asset_id FROM daily_expenses "
        "WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double old_amount = db_get_num(old_r, "amount");
    const char* old_type = csilk_json_get_string(old_r, "expense_type");
    int64_t old_asset_id = db_get_int(old_r, "asset_id");
    double old_delta = (old_type && strcmp(old_type, "income") == 0) ? old_amount : -old_amount;
```

- [ ] **Step 3: body 字段读取加 asset_id（L168-174 区域）**

原：
```c
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    csilk_json_t* tags = csilk_json_get(body, "tags");
```
改为（加 asset_id 与必填校验）：
```c
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    int64_t asset_id = (int64_t)csilk_json_get_number(body, "asset_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
    const char* date = csilk_json_get_string(body, "expense_date");
    const char* currency = csilk_json_get_string(body, "currency");
    const char* note = csilk_json_get_string(body, "note");
    csilk_json_t* tags = csilk_json_get(body, "tags");

    if (category_id <= 0 || asset_id <= 0 || !type || amount <= 0 || !date) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_bad_request(c, "asset_id、category_id、expense_type、amount、expense_date 为必填");
        return;
    }
```

- [ ] **Step 4: UPDATE 语句加 asset_id，并包裹事务 + 差量联动（L176-207 整体重写，含 `char sql[512];` 声明行 L176）**

> ⚠️ **替换范围含声明行**：`char sql[512];` 在 L176。整体替换 L176-207，**不要**从 L177 开始（否则重复声明，编译报错）。

原 UPDATE SQL 与 tags 同步块整体替换为：
```c
    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE daily_expenses SET category_id=%lld, asset_id=%lld, expense_type='%s', amount=%.2f, "
        "currency='%s', expense_date='%s', note='%s', updated_at=CURRENT_TIMESTAMP "
        "WHERE id=%s AND user_id=%lld",
        (long long)category_id, (long long)asset_id, type ? type : "", amount,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        id_str, (long long)user_id);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_db_exec(pool, sql);

    // 差量联动：新旧 delta 计算（income=+，expense=-）
    double new_delta = (strcmp(type, "income") == 0) ? amount : -amount;
    if (asset_id == old_asset_id) {
        // 同资产：合并为一次差值调用（产生 1 条审计）
        if (new_delta != old_delta) {
            if (balance_apply_delta(pool, asset_id, user_id, new_delta - old_delta,
                                    "daily_expense", atoll(id_str), note) != 0) {
                csilk_db_exec(pool, "ROLLBACK");
                csilk_json_free(body);
                csilk_json_free(old_row);
                respond_bad_request(c, "资产无效");
                return;
            }
        }
    } else {
        // 异资产：A 回退旧 delta，B 应用新 delta（产生 2 条审计）
        if (balance_apply_delta(pool, old_asset_id, user_id, -old_delta,
                                "daily_expense", atoll(id_str), note) != 0 ||
            balance_apply_delta(pool, asset_id, user_id, new_delta,
                                "daily_expense", atoll(id_str), note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    // Sync tags: delete existing links, then re-insert from body
    char del_tags_sql[256];
    snprintf(del_tags_sql, sizeof(del_tags_sql),
        "DELETE FROM expense_tags WHERE expense_id=%s", id_str);
    csilk_db_exec(pool, del_tags_sql);

    if (tags && csilk_json_is_array(tags)) {
        size_t n = csilk_json_array_size(tags);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* tag = csilk_json_array_get(tags, i);
            int64_t tag_id = (int64_t)csilk_json_get_number(tag, "id");
            if (tag_id <= 0) continue;

            char tag_sql[256];
            snprintf(tag_sql, sizeof(tag_sql),
                "INSERT OR IGNORE INTO expense_tags (expense_id, tag_id) "
                "VALUES (%s, %lld)",
                id_str, (long long)tag_id);
            csilk_db_exec(pool, tag_sql);
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    csilk_json_free(old_row);
    respond_ok_null(c);
```

> 注意：`atoll(id_str)` 代替原 expense_id——`id_str` 已校验为数字参数。`old_row` 在全部路径上释放（成功 COMMIT 后、校验失败提前 return 处均已 free）。

### Task 3.4: delete 事务 + 反转联动

**背景（已实测）**：delete 当前无事务（L213-231）。需：SELECT 旧记录 → BEGIN → 删 expense_tags + 删主记录 → balance_apply_delta(-旧delta) → COMMIT / 失败 ROLLBACK。

- [ ] **Step 1: 重写 daily_expenses_delete（L213-231）**

原函数整体替换为：
```c
void daily_expenses_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // 读取旧记录（反转联动需要）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT amount, expense_type, asset_id FROM daily_expenses "
        "WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double old_amount = db_get_num(old_r, "amount");
    const char* old_type = csilk_json_get_string(old_r, "expense_type");
    int64_t asset_id = db_get_int(old_r, "asset_id");
    double old_delta = (old_type && strcmp(old_type, "income") == 0) ? old_amount : -old_amount;

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char del_tags_sql[256];
    snprintf(del_tags_sql, sizeof(del_tags_sql),
        "DELETE FROM expense_tags WHERE expense_id=%s", id_str);
    csilk_db_exec(pool, del_tags_sql);

    char del_sql[256];
    snprintf(del_sql, sizeof(del_sql),
        "DELETE FROM daily_expenses WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, del_sql);

    // 反转旧 delta（支出反转 +，收入反转 -）
    if (balance_apply_delta(pool, asset_id, user_id, -old_delta,
                            "daily_expense", atoll(id_str), NULL) != 0) {
        csilk_db_exec(pool, "ROLLBACK");
        csilk_json_free(old_row);
        respond_error(c, 500, "删除失败");
        return;
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(old_row);
    respond_ok_null(c);
}
```

### Task 3.5: 构建 + 提交

- [ ] **Step 1: 构建**

```bash
cd backend/build && make 2>&1 | grep -E "error|warning" | head -20; echo "exit: $?"
```
Expected: 无 error 输出。

- [ ] **Step 2: Commit**

```bash
git add backend/src/daily_expenses.c
git commit -m "feat(daily-expenses): 收支联动资产余额——create/update/delete 事务+差量联动, list 加 asset_name"
```

---

## Chunk 4: transactions.c + assets.c 改造

**Files:**
- Modify: `backend/src/transactions.c`
- Modify: `backend/src/assets.c`

### Task 4.1: 新增 type_delta 映射辅助函数

**背景（已实测）**：transactions 的 `transaction_type` 有 9 种。delta 语义（spec §6.2 表）：deposit/income/sell = +；withdrawal/fee/loss/buy = −；buy/sell 金额优先级：有 quantity 且 price_per_unit 时用 `quantity × price_per_unit`，否则用 `amount`；transfer_in/transfer_out = 0（不联动）。

- [ ] **Step 1: 在 transactions.c 顶部（#include 之后）加静态辅助函数**

```c
/** @brief 计算交易对资产余额的业务方向 delta（transfer_* 返回 0）。 */
static double tx_delta(const char* type, double amount, double price, double qty) {
    if (!type) return 0;
    if (strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0) {
        return 0;  // 转账走 transfers 功能，不联动
    }
    if (strcmp(type, "buy") == 0) {
        double v = (qty > 0 && price > 0) ? qty * price : amount;
        return -v;  // 现金流出
    }
    if (strcmp(type, "sell") == 0) {
        double v = (qty > 0 && price > 0) ? qty * price : amount;
        return v;   // 现金流入
    }
    if (strcmp(type, "deposit") == 0 || strcmp(type, "income") == 0) {
        return amount;
    }
    // withdrawal / fee / loss
    return -amount;
}
```

### Task 4.2: create 事务 + 联动

**背景（已实测）**：create 校验 asset 归属后 INSERT（L88-102），**无 RETURNING**。需改为 RETURNING 拿新 id，BEGIN → INSERT → 若非 transfer_* 调用 balance_apply_delta → COMMIT / 失败 ROLLBACK。

- [ ] **Step 1: INSERT 改 RETURNING + 事务包裹（替换 L88-102）**

原：
```c
    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO transactions (user_id, asset_id, category_id, transaction_type, "
        "amount, price_per_unit, quantity, currency, transaction_date, note) "
        "VALUES (%lld, %lld, %lld, '%s', %.6f, %.4f, %.4f, '%s', '%s', '%s')",
        (long long)user_id, (long long)asset_id, (long long)category_id,
        type, amount, price, qty, currency, date, note ? note : "");

    if (csilk_db_exec(pool, sql) != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    csilk_json_free(body);
    respond_ok_null(c);
```
改为：
```c
    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO transactions (user_id, asset_id, category_id, transaction_type, "
        "amount, price_per_unit, quantity, currency, transaction_date, note) "
        "VALUES (%lld, %lld, %lld, '%s', %.6f, %.4f, %.4f, '%s', '%s', '%s') RETURNING id",
        (long long)user_id, (long long)asset_id, (long long)category_id,
        type, amount, price, qty, currency, date, note ? note : "");

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_json_t* ins = csilk_db_query_json(pool, sql);
    if (!ins || csilk_json_array_size(ins) == 0) {
        csilk_db_exec(pool, "ROLLBACK");
        if (ins) csilk_json_free(ins);
        csilk_json_free(body);
        respond_error(c, 500, "创建失败");
        return;
    }
    int64_t tx_id = db_get_int(csilk_json_array_get(ins, 0), "id");
    csilk_json_free(ins);

    // 联动资产余额（transfer_* 不联动）
    double tdelta = tx_delta(type, amount, price, qty);
    if (tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, tdelta,
                                "transaction", tx_id, note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    respond_ok_null(c);
```

### Task 4.3: update 事务 + 差值联动

**背景（已实测）**：update 不改 asset_id（SQL 无该字段）。需：SELECT 旧记录（amount, transaction_type, quantity, price_per_unit）→ BEGIN → UPDATE → 无条件计算 `新type_delta - 旧type_delta`（transfer 记 0，非transfer→transfer 时差值自然为 -旧delta，完成回退）→ 差值 != 0 时调用 → COMMIT / 失败 ROLLBACK。**不能用"若非 transfer_*"守卫跳过**。

- [ ] **Step 1: 校验后读取旧记录（L127 之后插入）**

在 `csilk_json_free(chk);` 之后插入：
```c
    // 读取旧记录（差值联动需要；quantity/price 用于重算旧 buy/sell 的 type_delta）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        csilk_json_free(body);
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    double old_tx_amount = db_get_num(old_r, "amount");
    const char* old_tx_type = csilk_json_get_string(old_r, "transaction_type");
    double old_tx_price = db_get_num(old_r, "price_per_unit");
    double old_tx_qty = db_get_num(old_r, "quantity");
    double old_tdelta = tx_delta(old_tx_type, old_tx_amount, old_tx_price, old_tx_qty);
```

- [ ] **Step 2: UPDATE 后事务包裹 + 差值联动（L137-148 区域重写）**

原：
```c
    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE transactions SET transaction_type='%s', amount=%.6f, price_per_unit=%.4f, "
        "quantity=%.4f, currency='%s', transaction_date='%s', note='%s', "
        "updated_at=CURRENT_TIMESTAMP WHERE id=%s AND user_id=%lld",
        type ? type : "", amount, price, qty,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        id_str, (long long)user_id);

    csilk_db_exec(pool, sql);
    csilk_json_free(body);
    respond_ok_null(c);
```
改为：
```c
    char sql[512];
    snprintf(sql, sizeof(sql),
        "UPDATE transactions SET transaction_type='%s', amount=%.6f, price_per_unit=%.4f, "
        "quantity=%.4f, currency='%s', transaction_date='%s', note='%s', "
        "updated_at=CURRENT_TIMESTAMP WHERE id=%s AND user_id=%lld",
        type ? type : "", amount, price, qty,
        currency ? currency : "CNY", date ? date : "", note ? note : "",
        id_str, (long long)user_id);

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(body);
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    csilk_db_exec(pool, sql);

    // 差值联动（transfer_* delta 记 0；非transfer→transfer 时差值=-旧delta 天然回退）
    double new_tdelta = tx_delta(type ? type : "", amount, price, qty);
    double diff = new_tdelta - old_tdelta;
    if (diff != 0) {
        if (balance_apply_delta(pool, db_get_int(old_r, "asset_id"), user_id, diff,
                                "transaction", atoll(id_str), note) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(body);
            csilk_json_free(old_row);
            respond_bad_request(c, "资产无效");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(body);
    csilk_json_free(old_row);
    respond_ok_null(c);
```

> 注意：旧记录 SELECT 未包含 asset_id 列——**需在 SELECT 中加 `asset_id`**（`db_get_int(old_r, "asset_id")` 需要它）。请将 Step 1 的 SELECT 改为 `"SELECT asset_id, amount, transaction_type, quantity, price_per_unit FROM transactions WHERE ..."`。

### Task 4.4: delete 事务 + 反转联动

**背景（已实测）**：delete 当前无事务（L151-163）。需：SELECT 旧记录 → BEGIN → DELETE → 若非 transfer_* 调用 balance_apply_delta(-旧delta) → COMMIT / 失败 ROLLBACK。

- [ ] **Step 1: 重写 transactions_delete（L151-163）**

原函数整体替换为：
```c
void transactions_delete(csilk_ctx_t* c) {
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) { respond_unauthorized(c); return; }

    const char* id_str = csilk_get_param(c, "id");
    if (!id_str) { respond_bad_request(c, "缺少 id"); return; }

    csilk_db_pool_t* pool = db_get_pool();

    // 读取旧记录（反转联动需要）
    char old_sql[256];
    snprintf(old_sql, sizeof(old_sql),
        "SELECT asset_id, amount, transaction_type, quantity, price_per_unit "
        "FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_json_t* old_row = csilk_db_query_json(pool, old_sql);
    if (!old_row || csilk_json_array_size(old_row) == 0) {
        if (old_row) csilk_json_free(old_row);
        respond_not_found(c);
        return;
    }
    const csilk_json_t* old_r = csilk_json_array_get(old_row, 0);
    int64_t asset_id = db_get_int(old_r, "asset_id");
    double old_tdelta = tx_delta(
        csilk_json_get_string(old_r, "transaction_type"),
        db_get_num(old_r, "amount"),
        db_get_num(old_r, "price_per_unit"),
        db_get_num(old_r, "quantity"));

    if (csilk_db_exec(pool, "BEGIN TRANSACTION") != 0) {
        csilk_json_free(old_row);
        respond_error(c, 500, "数据库错误");
        return;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM transactions WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);

    // 反转旧 delta（transfer_* 不联动）
    if (old_tdelta != 0) {
        if (balance_apply_delta(pool, asset_id, user_id, -old_tdelta,
                                "transaction", atoll(id_str), NULL) != 0) {
            csilk_db_exec(pool, "ROLLBACK");
            csilk_json_free(old_row);
            respond_error(c, 500, "删除失败");
            return;
        }
    }

    csilk_db_exec(pool, "COMMIT");
    csilk_json_free(old_row);
    respond_ok_null(c);
}
```

- [ ] **Step 2: transactions.c 顶部加 include**

```c
#include "common/balance.h"
```

### Task 4.5: assets_delete 返回值检查

**背景（已实测）**：assets_delete（L120-133）`csilk_db_exec(pool, sql); respond_ok_null(c);` 忽略返回值——FK 失败时静默报成功。spec §7 要求修复。

- [ ] **Step 1: 修改 assets_delete（L127-132）**

原：
```c
    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM assets WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    csilk_db_exec(pool, sql);
    respond_ok_null(c);
```
改为：
```c
    csilk_db_pool_t* pool = db_get_pool();
    char sql[256];
    snprintf(sql, sizeof(sql),
        "DELETE FROM assets WHERE id=%s AND user_id=%lld", id_str, (long long)user_id);
    if (csilk_db_exec(pool, sql) != 0) {
        respond_error(c, 500, "删除失败");
        return;
    }
    respond_ok_null(c);
```

### Task 4.6: 构建 + 提交

- [ ] **Step 1: 构建**

```bash
cd backend/build && make 2>&1 | grep -E "error|warning" | head -20; echo "exit: $?"
```
Expected: 无 error。

- [ ] **Step 2: Commit**

```bash
git add backend/src/transactions.c backend/src/assets.c
git commit -m "feat(transactions): 交易联动资产余额——create/update/delete 事务+差值联动, assets_delete 检查返回值"
```

---

## Chunk 5: 前端变更

**Files:**
- Modify: `frontend/src/types/index.ts`
- Modify: `frontend/src/views/DailyExpenses.vue`

### Task 5.1: DailyExpense 类型加 asset_id / asset_name

**Files:**
- Modify: `frontend/src/types/index.ts:60-73`（DailyExpense 接口）

- [ ] **Step 1: 修改 DailyExpense 接口**

原（L60-73）：
```ts
export interface DailyExpense {
  id: number;
  user_id: number;
  category_id: number;
  expense_type: ExpenseType;
  amount: number;
  currency: string;
  expense_date: string;
  note?: string;
  tags?: Tag[];
  category_name?: string;
  created_at: string;
  updated_at: string;
}
```
改为：
```ts
export interface DailyExpense {
  id: number;
  user_id: number;
  category_id: number;
  asset_id: number;
  asset_name?: string;
  expense_type: ExpenseType;
  amount: number;
  currency: string;
  expense_date: string;
  note?: string;
  tags?: Tag[];
  category_name?: string;
  created_at: string;
  updated_at: string;
}
```

### Task 5.2: DailyExpenses.vue 表单加资产选择器 + 列表加关联资产列

**Files:**
- Modify: `frontend/src/views/DailyExpenses.vue`

- [ ] **Step 1: import assetsApi 与 Asset 类型（L148-153 区域）**

原：
```ts
import { dailyExpensesApi } from '@/api/daily_expenses'
import { categoriesApi } from '@/api/categories'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import type { DailyExpense, Tag, Category } from '@/types'
```
改为：
```ts
import { dailyExpensesApi } from '@/api/daily_expenses'
import { categoriesApi } from '@/api/categories'
import { assetsApi } from '@/api/assets'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import type { DailyExpense, Tag, Category, Asset } from '@/types'
```

- [ ] **Step 2: 加 assets 列表 state（L156 附近）**

在 `const allCategories = ref<Category[]>([])` 后加：
```ts
const allAssets = ref<Asset[]>([])
```

- [ ] **Step 3: form 加 asset_id 字段与校验规则（L188-189）**

原：
```ts
const form = reactive({ expense_type: 'expense' as 'income' | 'expense', category_id: null as number | null, amount: 0, expense_date: '', note: '', tags: [] as Tag[], _catPath: [] as number[] })
const rules = { expense_type: [{ required: true }], category_id: [{ required: true }], amount: [{ required: true }], expense_date: [{ required: true }] }
```
改为：
```ts
const form = reactive({ expense_type: 'expense' as 'income' | 'expense', category_id: null as number | null, asset_id: null as number | null, amount: 0, expense_date: '', note: '', tags: [] as Tag[], _catPath: [] as number[] })
const rules = { expense_type: [{ required: true }], category_id: [{ required: true }], asset_id: [{ required: true, message: '请选择关联资产', trigger: 'change' }], amount: [{ required: true }], expense_date: [{ required: true }] }
```

- [ ] **Step 4: openDialog 回显 asset_id（L213-217）**

原：
```ts
  Object.assign(form, expense ? { expense_type: expense.expense_type, category_id: expense.category_id, amount: expense.amount, expense_date: expense.expense_date, note: expense.note, tags: expense.tags ?? [], _catPath: [expense.category_id] }
    : { expense_type: 'expense', category_id: null, amount: 0, expense_date: new Date().toISOString().slice(0, 10), note: '', tags: [], _catPath: [] })
```
改为：
```ts
  Object.assign(form, expense ? { expense_type: expense.expense_type, category_id: expense.category_id, asset_id: expense.asset_id, amount: expense.amount, expense_date: expense.expense_date, note: expense.note, tags: expense.tags ?? [], _catPath: [expense.category_id] }
    : { expense_type: 'expense', category_id: null, asset_id: null, amount: 0, expense_date: new Date().toISOString().slice(0, 10), note: '', tags: [], _catPath: [] })
```

- [ ] **Step 5: onMounted 加载资产列表（L242-247）**

原：
```ts
onMounted(async () => {
  const res = await categoriesApi.list({ type: 'income,expense' })
  allCategories.value = res
  filters.month = new Date().toISOString().slice(0, 7)
  loadData()
})
```
改为：
```ts
onMounted(async () => {
  const [catRes, assetRes] = await Promise.all([
    categoriesApi.list({ type: 'income,expense' }),
    assetsApi.list(),
  ])
  allCategories.value = catRes
  allAssets.value = assetRes
  filters.month = new Date().toISOString().slice(0, 7)
  loadData()
})
```

- [ ] **Step 6: 表单模板加资产选择器（L119-121 分类之后）**

在分类 el-form-item 之后、金额之前插入：
```html
        <el-form-item label="关联资产" prop="asset_id">
          <el-select v-model="form.asset_id" placeholder="选择资产" style="width: 100%" filterable>
            <el-option v-for="a in allAssets" :key="a.id" :label="`${a.name}（${a.currency} ${a.current_value.toFixed(2)}）`" :value="a.id">
              <span>{{ a.name }}</span>
              <el-tag v-if="a.asset_type === 'loan' || a.asset_type === 'credit_card' || a.asset_type === 'other_liability'" size="small" type="warning" effect="light" style="margin-left: 8px">负债</el-tag>
              <span style="float: right; color: #8492a6; font-size: 13px">{{ a.currency }} {{ a.current_value.toFixed(2) }}</span>
            </el-option>
          </el-select>
        </el-form-item>
```

> 说明：`Asset` 接口已有 `asset_type?`、`currency`、`current_value` 字段（探索已确认）。选项显示资产名 + 货币 + 当前余额；负债类资产（loan/credit_card/other_liability）加"负债"角标（spec §8.2）。

- [ ] **Step 7: 列表加"关联资产"列（L58-59 日期之后）**

在日期列之后插入：
```html
              <el-table-column prop="asset_name" label="关联资产" min-width="110" />
```

### Task 5.4: Transactions.vue 编辑对话框加余额同步提示

**Files:**
- Modify: `frontend/src/views/Transactions.vue:78-109`（编辑对话框 el-form 顶部）

**背景**：spec §8.3——交易记录已有 `asset_id` 选择器（无需新增），但编辑时需提示用户"修改金额将同步调整资产余额"。该提示仅在编辑模式（`editingId` 非空）显示。

- [ ] **Step 1: 在 el-form 顶部（L80 资产选择器之前）插入条件提示**

在 `el-dialog` 内 `el-form` 的 `<el-form-item label="资产"` 之前插入：
```html
        <el-alert v-if="editingId" type="info" :closable="false" show-icon
                  title="修改金额/类型将同步调整关联资产的余额" style="margin-bottom: 16px" />
```

> 说明：`editingId` 为现有 ref（L69 `@click="openDialog(row)"` 设置，L78 `editingId ? '编辑交易' : '新增交易'`），无需新增状态。`el-alert` 是 Element Plus 全局组件（main.ts 全量注册，已确认），无需额外 import。

- [ ] **Step 2: 前端类型检查**

```bash
cd frontend && npx vue-tsc --noEmit 2>&1 | tail -5
```
Expected: 无 error。

- [ ] **Step 3: Commit**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(frontend): 交易编辑对话框提示余额同步（spec 8.3）"
```

### Task 5.3: 前端类型检查 + 构建

- [ ] **Step 1: 类型检查**

```bash
cd frontend && npx vue-tsc --noEmit 2>&1 | tail -20
```
Expected: 无 error（可能显示既有 warnings，与本次改动无关）。

- [ ] **Step 2: 生产构建**

```bash
cd frontend && npm run build 2>&1 | tail -10
```
Expected: 构建成功，vite 输出 dist 产物。

- [ ] **Step 3: Commit**

```bash
git add frontend/src/types/index.ts frontend/src/views/DailyExpenses.vue
git commit -m "feat(frontend): 收支表单加必选资产选择器, 列表显示关联资产"
```

---

## Chunk 6: 集成测试脚本

**Files:**
- Create: `backend/tests/test_link.sh`

**背景**：项目无测试框架，采用真实服务器 + curl + sqlite3 CLI 的集成测试。脚本需可重复运行（每次用全新临时 DB）。覆盖 spec §9.2 全部场景 + §9.1 负债方向。**注意**：脚本依赖服务器在 8080 端口启动，若端口被占需先清理。

- [ ] **Step 1: 写测试脚本**

```bash
#!/usr/bin/env bash
# 收支-资产联动集成测试
# 用法: ./test_link.sh   (需已在 backend/build 构建过 minefolio)
set -euo pipefail

BASE="http://localhost:8080/api"   # 后端硬编码 8080（main.c:234），运行前确认 8080 空闲
DB="/tmp/mf_link_test.db"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
PASS=0; FAIL=0

cleanup() {
  [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null || true
  rm -f "$DB"
}
trap cleanup EXIT

# --- 启动服务器 ---
rm -f "$DB"
cd "$BUILD_DIR"
MINEFOLIO_DB_DSN="$DB" ./minefolio &
SERVER_PID=$!
sleep 1

req() { # req METHOD PATH JSON
  local method="$1" path="$2" data="${3:-}"
  if [ -n "$data" ]; then
    curl -s -X "$method" -H "Content-Type: application/json" "$BASE$path" -d "$data"
  else
    curl -s -X "$method" "$BASE$path"
  fi
}

check() { # check DESC EXPECTED ACTUAL
  if [ "$2" = "$3" ]; then PASS=$((PASS+1)); echo "  ✅ $1"; else FAIL=$((FAIL+1)); echo "  ❌ $1 (期望 $2 实际 $3)"; fi
}

echo "== 1. 注册 + 登录 =="
req POST /auth/register '{"username":"linktest","password":"pass1234"}' >/dev/null
TOKEN=$(req POST /auth/login '{"username":"linktest","password":"pass1234"}' | jq -r '.data.token')
AUTH="Authorization: Bearer $TOKEN"
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"日常消费","type":"expense","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"工资","type":"income","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"现金","type":"asset","currency":"CNY"}' >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/categories" -d '{"name":"信用卡","type":"asset","asset_type":"credit_card","currency":"CNY"}' >/dev/null
# 读取真实分类 id（避免依赖插入顺序）
EXPENSE_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='日常消费'")
INCOME_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='工资'")
ASSET_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='现金'")
CC_CAT=$(sqlite3 "$DB" "SELECT id FROM categories WHERE name='信用卡'")

echo "== 2. 建资产 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets" -d "{\"name\":\"钱包\",\"category_id\":$ASSET_CAT,\"current_value\":10000,\"currency\":\"CNY\"}" >/dev/null
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/assets" -d "{\"name\":\"信用卡\",\"category_id\":$CC_CAT,\"current_value\":0,\"currency\":\"CNY\"}" >/dev/null
# 用 sqlite3 直接取真实 id（避免依赖 API 返回）
WALLET_ID=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='钱包' AND category_id=$ASSET_CAT")
CC_ID=$(sqlite3 "$DB" "SELECT id FROM assets WHERE name='信用卡' AND category_id=$CC_CAT")

echo "== 3. 记收入 500 → 余额 10500 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "普通资产收入+500" "10500.0" "$BAL"
LOG=$(sqlite3 "$DB" "SELECT delta FROM asset_balance_logs WHERE source_type='daily_expense' ORDER BY id DESC LIMIT 1")
check "审计 delta=+500" "500.0" "$LOG"

echo "== 4. 记支出 300 → 余额 10200 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":300,\"currency\":\"CNY\",\"expense_date\":\"2026-08-02\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "普通资产支出-300" "10200.0" "$BAL"

echo "== 5. 更新收入 500→800 → 余额 10500 =="
EXP_ID=$(sqlite3 "$DB" "SELECT id FROM daily_expenses WHERE expense_type='income' LIMIT 1")
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses/$EXP_ID" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "更新同资产合并差量+300" "10500.0" "$BAL"

echo "== 6. 删除支出 300 → 余额 10800 =="
DEL_ID=$(sqlite3 "$DB" "SELECT id FROM daily_expenses WHERE expense_type='expense' LIMIT 1")
curl -s -X DELETE -H "$AUTH" "$BASE/daily-expenses/$DEL_ID" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "删除支出反转+300" "10800.0" "$BAL"

echo "== 7. 信用卡：刷卡支出 500 → 欠款 +500 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$CC_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-03\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$CC_ID")
check "负债支出→余额+500" "500.0" "$BAL"

echo "== 8. 信用卡还款 500 → 欠款 0 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$CC_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":500,\"currency\":\"CNY\",\"expense_date\":\"2026-08-04\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$CC_ID")
check "负债还款→余额-500" "0.0" "$BAL"

echo "== 9. 余额不足允许负数 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$EXPENSE_CAT,\"expense_type\":\"expense\",\"amount\":20000,\"currency\":\"CNY\",\"expense_date\":\"2026-08-05\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "允许负数" "-9200.0" "$BAL"

echo "== 10. 非法资产 → code 1002 且原子回滚 =="
BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
CODE=$(curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses" -d '{"asset_id":99999,"category_id":1,"expense_type":"expense","amount":100,"currency":"CNY","expense_date":"2026-08-06"}' | jq -r '.code')
AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM daily_expenses")
check "非法资产 code=1002" "1002" "$CODE"
check "非法资产主记录不落库" "$BEFORE" "$AFTER"
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "非法资产余额不变" "-9200.0" "$BAL"

echo "== 11. 交易联动：存款 +1000 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"deposit\",\"amount\":1000,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-07\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "存款+1000" "-8200.0" "$BAL"

echo "== 12. 转账不联动 =="
curl -s -H "$AUTH" -H "Content-Type: application/json" "$BASE/transactions" -d "{\"asset_id\":$WALLET_ID,\"category_id\":$ASSET_CAT,\"transaction_type\":\"transfer_out\",\"amount\":100,\"currency\":\"CNY\",\"transaction_date\":\"2026-08-08\"}" >/dev/null
BAL=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
check "转账不联动" "-8200.0" "$BAL"

echo "== 13. 更新时切换关联资产 A→B（钱包→信用卡）=="
# 将步骤 5 的收入记录（800，原资产=钱包）切到信用卡：
# 钱包回退旧 delta（-800 → 余额 -9000），信用卡应用新 delta（+800 → 余额 800），审计 +2 条
LOG_CNT_BEFORE=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
curl -s -X PUT -H "$AUTH" -H "Content-Type: application/json" "$BASE/daily-expenses/$EXP_ID" -d "{\"asset_id\":$CC_ID,\"category_id\":$INCOME_CAT,\"expense_type\":\"income\",\"amount\":800,\"currency\":\"CNY\",\"expense_date\":\"2026-08-01\"}" >/dev/null
BAL_A=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$WALLET_ID")
BAL_B=$(sqlite3 "$DB" "SELECT current_value FROM assets WHERE id=$CC_ID")
LOG_CNT_AFTER=$(sqlite3 "$DB" "SELECT COUNT(*) FROM asset_balance_logs")
check "A 钱包回退 -800" "-9000.0" "$BAL_A"
check "B 信用卡应用 +800" "800.0" "$BAL_B"
check "切换产生 2 条审计" "$((LOG_CNT_BEFORE + 2))" "$LOG_CNT_AFTER"

echo ""
echo "结果: PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
```

> **端口**：后端硬编码 8080（main.c:234，不读 `MINEFOLIO_PORT`），脚本直连 8080——运行前确认无 dev 服务器占用（`lsof -i :8080` 检查）。**category_id 已改为 sqlite3 读取真实 id**（EXPENSE_CAT/INCOME_CAT/ASSET_CAT/CC_CAT），不依赖插入顺序。**非法资产断言用 body 的 `code` 字段**（后端所有响应均 HTTP 200，错误码在 JSON body：respond_bad_request=1002）。

- [ ] **Step 2: 脚本可执行 + 运行**

```bash
chmod +x backend/tests/test_link.sh && backend/tests/test_link.sh
```
Expected: 全部 ✅（PASS=15 FAIL=0）。

- [ ] **Step 3: 若个别断言失败，按错误定位修复**

常见失败点与对策：
- 端口 8080 被占 → `lsof -i :8080` 查找占用进程，停止 dev 服务器后重跑
- 分类/资产 id 为空 → 检查 register/login 是否成功（TOKEN 是否取出），分类创建请求是否返回错误
- 服务器启动失败 → 检查构建是否最新（`cd backend/build && make`）

- [ ] **Step 4: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test: 收支-资产联动集成测试脚本（余额/审计/负债反转/原子性/交易联动）"
```

---

## 验证清单（最终）

实现完成后，执行以下全部验证：

1. `cd backend/build && make` 无 error
2. `backend/tests/test_link.sh` PASS 全部通过
3. 存量库迁移验证（Task 1.2 Step 3/4）
4. `cd frontend && npx vue-tsc --noEmit && npm run build` 无 error
5. 手工冒烟：`cd backend/build && MINEFOLIO_DB_DSN=/tmp/mf_smoke.db ./minefolio` + 前端 `npm run dev`，在收支页创建一条收入记录，确认资产余额在资产页同步变化、列表显示关联资产列
