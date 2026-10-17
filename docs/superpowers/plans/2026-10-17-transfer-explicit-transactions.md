# 转账辅助表显式双向外键与 V014 数据库迁移 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `transfers` 表引入 `from_tx_id` 与 `to_tx_id` 显式外键与 V014 数据库迁移（SQLite + PostgreSQL），重排转账创建写入时序，将交易删除从“启发式推断匹配”改造为精准的物理主键删除，彻底解决同账户同金额重复划转误删缺陷。

**Architecture:**
1. **数据库迁移层**：编写 `V014__transfer_explicit_transactions.sql`（SQLite 采用无锁重构表并回填，PostgreSQL 采用 `ALTER TABLE ADD COLUMN` 并回填），同步更新基线脚本 `migration.sql` 与 `migration_postgres.sql`。
2. **领域层与仓储层**：`mf_transfer_t` 实体增加 `from_tx_id` 与 `to_tx_id`；`mf_transfer_repo_insert` 插入时完整持久化外键。
3. **业务用例编排层**：`transfer_usecase_create` 事务内时序调整为 `transfer_out` -> `transfer_in` -> `transfers`；`tx_usecase_delete` 交易删除使用 `WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?)` 精确删除，保留历史兼容兜底。
4. **测试与质量保证**：在 `test_domain_repositories.c` 中添加同账户同金额重复划转精准删除验证用例；更新 `test_migration_engine.c`；保证 33 个 CTest 单元测试 + 143 处断言端到端集成测试全绿。

**Tech Stack:** C23, GCC 16 / Linux, SQLite 3, PostgreSQL, csilk v0.5.2, CTest, CMake.

---

### File Structure Map

| 目标文件 | 责任与变更范围 |
|---|---|
| `backend/sql/migrations/sqlite/V014__transfer_explicit_transactions.sql` | 新增 SQLite V014 迁移脚本（无锁重构 transfers 表、级联外键、历史数据回填、双向索引） |
| `backend/sql/migrations/postgres/V014__transfer_explicit_transactions.sql` | 新增 PostgreSQL V014 迁移脚本（ALTER TABLE 加列、级联外键、历史数据回填、双向索引） |
| `backend/sql/migration.sql` | 更新全新安装 SQLite 基线建表语句（追加 from_tx_id, to_tx_id 及索引） |
| `backend/sql/migration_postgres.sql` | 更新全新安装 PostgreSQL 基线建表语句（追加 from_tx_id, to_tx_id 及索引） |
| `backend/tests/unit/test_migration_engine.c` | 校准迁移脚本发现断言 `count >= 14` |
| `backend/src/domain/transfer/entity.h` | `mf_transfer_t` 结构体新增 `from_tx_id` 与 `to_tx_id` |
| `backend/src/infrastructure/repositories/transfer_repo_impl.c` | `mf_transfer_repo_insert` 写入 SQL 增加 `from_tx_id` 与 `to_tx_id` 参数绑定 |
| `backend/src/application/transfer/usecases.c` | `transfer_usecase_create` 事务内写入时序重排（先交易后辅助表） |
| `backend/src/application/transaction/usecases.c` | `tx_usecase_delete` 清理 `transfers` 改造为按 `from_tx_id OR to_tx_id` 精确删除 |
| `backend/tests/unit/test_domain_repositories.c` | 新增同账户同金额并发转账防误删专项测试用例 |

---

### Task 1: 数据库迁移脚本编写与基线同步 (V014 SQLite + PostgreSQL)

**Files:**
- Create: `backend/sql/migrations/sqlite/V014__transfer_explicit_transactions.sql`
- Create: `backend/sql/migrations/postgres/V014__transfer_explicit_transactions.sql`
- Modify: `backend/sql/migration.sql:87-98`
- Modify: `backend/sql/migration_postgres.sql:86-97`
- Modify: `backend/tests/unit/test_migration_engine.c:302`

- [ ] **Step 1: 创建 SQLite V014 迁移脚本**

创建 `backend/sql/migrations/sqlite/V014__transfer_explicit_transactions.sql`：
```sql
-- V014: 转账表显式双向关联 transactions 外键与历史数据回填
CREATE TABLE IF NOT EXISTS transfers_new (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     INTEGER REFERENCES ledgers(id) ON DELETE SET NULL,
    from_tx_id    INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    to_tx_id      INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 回填已有历史转账记录的外键关联
INSERT INTO transfers_new (id, user_id, from_asset_id, to_asset_id, amount, currency, transfer_date, note, ledger_id, from_tx_id, to_tx_id, created_at)
SELECT t.id, t.user_id, t.from_asset_id, t.to_asset_id, t.amount, t.currency, t.transfer_date, t.note, t.ledger_id,
       (SELECT tx.id FROM transactions tx
        WHERE tx.user_id = t.user_id
          AND tx.transaction_type = 'transfer_out'
          AND tx.asset_id = t.from_asset_id
          AND tx.linked_asset_id = t.to_asset_id
          AND ABS(tx.amount - t.amount) < 0.000001
        ORDER BY tx.id DESC LIMIT 1),
       (SELECT tx.id FROM transactions tx
        WHERE tx.user_id = t.user_id
          AND tx.transaction_type = 'transfer_in'
          AND tx.asset_id = t.to_asset_id
          AND tx.linked_asset_id = t.from_asset_id
          AND ABS(tx.amount - t.amount) < 0.000001
        ORDER BY tx.id DESC LIMIT 1),
       t.created_at
FROM transfers t;

DROP TABLE IF EXISTS transfers;
ALTER TABLE transfers_new RENAME TO transfers;

CREATE INDEX IF NOT EXISTS idx_transfers_user ON transfers(user_id);
CREATE INDEX IF NOT EXISTS idx_transfers_ledger ON transfers(ledger_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_asset ON transfers(from_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_asset ON transfers(to_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
```

- [ ] **Step 2: 创建 PostgreSQL V014 迁移脚本**

创建 `backend/sql/migrations/postgres/V014__transfer_explicit_transactions.sql`：
```sql
-- V014: 转账表显式双向关联 transactions 外键与历史数据回填
ALTER TABLE transfers ADD COLUMN IF NOT EXISTS from_tx_id BIGINT REFERENCES transactions(id) ON DELETE CASCADE;
ALTER TABLE transfers ADD COLUMN IF NOT EXISTS to_tx_id BIGINT REFERENCES transactions(id) ON DELETE CASCADE;

-- 回填已有历史记录
UPDATE transfers t
SET from_tx_id = (
    SELECT tx.id FROM transactions tx
    WHERE tx.user_id = t.user_id
      AND tx.transaction_type = 'transfer_out'
      AND tx.asset_id = t.from_asset_id
      AND tx.linked_asset_id = t.to_asset_id
      AND ABS(tx.amount - t.amount) < 0.000001
    ORDER BY tx.id DESC LIMIT 1
)
WHERE t.from_tx_id IS NULL;

UPDATE transfers t
SET to_tx_id = (
    SELECT tx.id FROM transactions tx
    WHERE tx.user_id = t.user_id
      AND tx.transaction_type = 'transfer_in'
      AND tx.asset_id = t.to_asset_id
      AND tx.linked_asset_id = t.from_asset_id
      AND ABS(tx.amount - t.amount) < 0.000001
    ORDER BY tx.id DESC LIMIT 1
)
WHERE t.to_tx_id IS NULL;

CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
```

- [ ] **Step 3: 更新 SQLite 与 PostgreSQL 基线初始化模式**

更新 `backend/sql/migration.sql`：
```sql
CREATE TABLE IF NOT EXISTS transfers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     INTEGER REFERENCES ledgers(id) ON DELETE SET NULL,
    from_tx_id    INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    to_tx_id      INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
```

更新 `backend/sql/migration_postgres.sql`：
```sql
CREATE TABLE IF NOT EXISTS transfers (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     BIGINT REFERENCES ledgers(id) ON DELETE SET NULL,
    from_tx_id    BIGINT REFERENCES transactions(id) ON DELETE CASCADE,
    to_tx_id      BIGINT REFERENCES transactions(id) ON DELETE CASCADE,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
```

- [ ] **Step 4: 校准 `test_migration_engine.c` 断言**

将 `backend/tests/unit/test_migration_engine.c` 行 302 的 `assert(count >= 13);` 校准为 `assert(count >= 14);`。

- [ ] **Step 5: 编译并运行迁移单元测试**

```bash
cmake --build backend/build --target test_migration_engine && ./backend/build/test_migration_engine
```
Expected: PASS，14 个迁移脚本正确被发现并应用。

- [ ] **Step 6: 提交 Task 1 代码**

```bash
git add backend/sql/migrations/sqlite/V014__transfer_explicit_transactions.sql \
        backend/sql/migrations/postgres/V014__transfer_explicit_transactions.sql \
        backend/sql/migration.sql \
        backend/sql/migration_postgres.sql \
        backend/tests/unit/test_migration_engine.c
git commit -m "feat(migration): 🎸 add V014 migration for transfers explicit transaction foreign keys"
```

---

### Task 2: 领域实体与仓储层 SQL 扩展

**Files:**
- Modify: `backend/src/domain/transfer/entity.h:13-24`
- Modify: `backend/src/infrastructure/repositories/transfer_repo_impl.c:34-70`

- [ ] **Step 1: 扩展 `mf_transfer_t` 结构体**

在 `backend/src/domain/transfer/entity.h` 中追加 `from_tx_id` 与 `to_tx_id`：
```c
typedef struct {
    int64_t id;                /**< 主键 ID */
    int64_t user_id;           /**< 用户 ID */
    int64_t from_asset_id;     /**< 转出资产 ID */
    int64_t to_asset_id;       /**< 转入资产 ID */
    double  amount;            /**< 划转金额 */
    char    currency[8];       /**< 货币代码 */
    char    transfer_date[16]; /**< 转账日期 (YYYY-MM-DD) */
    char    note[256];         /**< 备注 */
    int64_t ledger_id;         /**< 账本 ID */
    int64_t from_tx_id;        /**< 关联 transfer_out 交易 ID */
    int64_t to_tx_id;          /**< 关联 transfer_in 交易 ID */
    char    created_at[32];    /**< 创建时间 */
} mf_transfer_t;
```

- [ ] **Step 2: 修改 `mf_transfer_repo_insert` SQL 绑定**

在 `backend/src/infrastructure/repositories/transfer_repo_impl.c` 中：
```c
int64_t
mf_transfer_repo_insert(void* db_pool, int64_t user_id, const mf_transfer_t* transfer)
{
    csilk_db_pool_t* pool = (csilk_db_pool_t*)db_pool;
    char uid[32], fid[32], tid[32], amt[64], lid[32], from_tx[32], to_tx[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(fid, sizeof(fid), "%lld", (long long)transfer->from_asset_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)transfer->to_asset_id);
    snprintf(amt, sizeof(amt), "%.6f", transfer->amount);
    snprintf(lid, sizeof(lid), "%lld", (long long)transfer->ledger_id);
    snprintf(from_tx, sizeof(from_tx), "%lld", (long long)transfer->from_tx_id);
    snprintf(to_tx, sizeof(to_tx), "%lld", (long long)transfer->to_tx_id);

    csilk_json_t* res = csilk_db_query_param_json(
        pool,
        "INSERT INTO transfers (user_id, from_asset_id, to_asset_id, amount, currency, "
        "transfer_date, note, ledger_id, from_tx_id, to_tx_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, "
        "COALESCE(CAST(NULLIF(?, '0') AS BIGINT), (SELECT ledger_id FROM assets WHERE id=?)), "
        "CAST(NULLIF(?, '0') AS BIGINT), "
        "CAST(NULLIF(?, '0') AS BIGINT)) "
        "RETURNING id",
        (const char*[]){uid,
                        fid,
                        tid,
                        amt,
                        transfer->currency[0] ? transfer->currency : "CNY",
                        transfer->transfer_date,
                        transfer->note[0] ? transfer->note : "",
                        lid,
                        fid,
                        from_tx,
                        to_tx,
                        NULL});
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) {
        id = db_get_int(csilk_json_array_get(res, 0), "id");
    }
    if (res) {
        csilk_json_free(res);
    }
    return id;
}
```

- [ ] **Step 3: 编译检查仓储层编译通过**

```bash
cmake --build backend/build --parallel
```
Expected: 编译 0 error。

- [ ] **Step 4: 提交 Task 2 代码**

```bash
git add backend/src/domain/transfer/entity.h \
        backend/src/infrastructure/repositories/transfer_repo_impl.c
git commit -m "feat(transfer): 🎸 persist from_tx_id and to_tx_id in transfer repository"
```

---

### Task 3: 转账写入时序重排与交易删除用例精确匹配

**Files:**
- Modify: `backend/src/application/transfer/usecases.c:55-115`
- Modify: `backend/src/application/transaction/usecases.c:383-391,432-440`

- [ ] **Step 1: 重排 `transfer_usecase_create` 事务内创建时序**

在 `backend/src/application/transfer/usecases.c` 中：
```c
    const char* cur = cmd->currency && cmd->currency[0] ? cmd->currency : "CNY";
    const char* note = cmd->note ? cmd->note : "";

    /* 1. Record transfer_out transaction first */
    int64_t out_tx_id = mf_transfer_repo_insert_out_transaction(pool,
                                                                cmd->user_id,
                                                                cmd->from_asset_id,
                                                                cmd->to_asset_id,
                                                                cmd->amount,
                                                                cur,
                                                                cmd->transfer_date,
                                                                note,
                                                                cmd->ledger_id);
    if (out_tx_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 2. Record transfer_in transaction (paired with out_tx_id as parent_tx_id) */
    int64_t in_tx_id = mf_transfer_repo_insert_in_transaction(pool,
                                                              cmd->user_id,
                                                              cmd->to_asset_id,
                                                              cmd->from_asset_id,
                                                              cmd->amount,
                                                              cur,
                                                              cmd->transfer_date,
                                                              note,
                                                              out_tx_id,
                                                              cmd->ledger_id);
    if (in_tx_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "记录划转交易失败");
        return -1;
    }

    /* 3. Insert transfer record with explicit from_tx_id and to_tx_id */
    mf_transfer_t transfer = {0};
    transfer.user_id = cmd->user_id;
    transfer.from_asset_id = cmd->from_asset_id;
    transfer.to_asset_id = cmd->to_asset_id;
    transfer.amount = cmd->amount;
    transfer.ledger_id = cmd->ledger_id;
    transfer.from_tx_id = out_tx_id;
    transfer.to_tx_id = in_tx_id;
    strncpy(transfer.currency, cur, sizeof(transfer.currency) - 1);
    strncpy(transfer.transfer_date, cmd->transfer_date, sizeof(transfer.transfer_date) - 1);
    if (cmd->note) {
        strncpy(transfer.note, cmd->note, sizeof(transfer.note) - 1);
    }

    int64_t transfer_id = mf_transfer_repo_insert(pool, cmd->user_id, &transfer);
    if (transfer_id <= 0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "转账失败");
        return -1;
    }

    /* 4. Apply balance via Ledger Engine */
    currency_t cur_t = currency_from_str(cur);
    money_t    amt_m;
    money_from_double(cmd->amount, cur_t, &amt_m);

    if (ledger_apply_transfer(
            pool, cmd->user_id, cmd->from_asset_id, cmd->to_asset_id, amt_m, transfer_id, note) !=
        0) {
        csilk_db_exec(pool, "ROLLBACK");
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "资产余额更新失败");
        return -1;
    }
```

- [ ] **Step 2: 在 `tx_usecase_delete` 中精确清理 `transfers`**

在 `backend/src/application/transaction/usecases.c` 中：
修改 `is_transfer_out` 清理块：
```c
        /* 4. 清理 transfers 记录 (优先精准按外键删除，兼容兜底历史遗留行) */
        csilk_json_t* del_res = csilk_db_query_param_json(
            pool,
            "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?) RETURNING id",
            (const char*[]){uid_str, tid_str, tid_str, NULL});
        if (!del_res || csilk_json_array_size(del_res) == 0) {
            // 历史遗留行兜底（未回填外键的数据）
            csilk_json_free(csilk_db_query_param_json(
                pool,
                "DELETE FROM transfers WHERE id IN ("
                "  SELECT id FROM transfers WHERE user_id=? AND "
                "from_asset_id=? AND to_asset_id=? AND amount=? "
                "  ORDER BY id DESC LIMIT 1)",
                (const char*[]){uid_str, aid_str, laid_str, amt_str, NULL}));
        }
        if (del_res) {
            csilk_json_free(del_res);
        }
```

修改 `is_transfer_in` 清理块：
```c
            // 清理 transfers (优先按主单或子单 ID 精准删除，兼容兜底)
            const char* target_tx_id = (main_out_id > 0) ? maid_str : tid_str;
            csilk_json_t* del_res = csilk_db_query_param_json(
                pool,
                "DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?) RETURNING id",
                (const char*[]){uid_str, target_tx_id, target_tx_id, NULL});
            if (!del_res || csilk_json_array_size(del_res) == 0) {
                csilk_json_free(csilk_db_query_param_json(
                    pool,
                    "DELETE FROM transfers WHERE id IN ("
                    "  SELECT id FROM transfers WHERE user_id=? AND from_asset_id=? AND to_asset_id=? "
                    "AND amount=? "
                    "  ORDER BY id DESC LIMIT 1)",
                    (const char*[]){uid_str, laid_str, aid_str, amt_str, NULL}));
            }
            if (del_res) {
                csilk_json_free(del_res);
            }
```

- [ ] **Step 3: 编译后端并确保通过**

```bash
cmake --build backend/build --parallel
```
Expected: PASS，0 warnings / 0 errors。

- [ ] **Step 4: 提交 Task 3 代码**

```bash
git add backend/src/application/transfer/usecases.c \
        backend/src/application/transaction/usecases.c
git commit -m "refactor(transfer): ♻️ reschedule transfer write order and enable deterministic transfer deletion"
```

---

### Task 4: 新增同金额重复转账单元测试与全量回归测试

**Files:**
- Modify: `backend/tests/unit/test_domain_repositories.c`

- [ ] **Step 1: 在 `test_domain_repositories.c` 中添加防误删专项用例**

在 `test_domain_repositories.c` 的 `test_tx_monthly_excludes_transfers` 前添加新测试函数 `test_transfer_identical_duplicate_deletion`：
```c
static void
test_transfer_identical_duplicate_deletion(csilk_db_pool_t* pool, int64_t user_id)
{
    printf("--- Testing Identical Duplicate Transfer Precise Deletion ---\n");

    // 1. 创建两个资产
    int64_t from_asset = create_test_asset(pool, user_id, "Dup From Account", "cash", 1000.0, "CNY");
    int64_t to_asset = create_test_asset(pool, user_id, "Dup To Account", "bank", 1000.0, "CNY");

    // 2. 连续创建两笔金额完全相同的转账 (均为 300.0 CNY)
    create_transfer_cmd_t cmd1 = {
        .user_id = user_id,
        .from_asset_id = from_asset,
        .to_asset_id = to_asset,
        .amount = 300.0,
        .currency = "CNY",
        .transfer_date = "2026-10-17",
        .note = "Transfer 1",
        .ledger_id = 100
    };
    transfer_usecase_result_t res1 = {0};
    assert(transfer_usecase_create(pool, &cmd1, &res1) == 0);

    create_transfer_cmd_t cmd2 = {
        .user_id = user_id,
        .from_asset_id = from_asset,
        .to_asset_id = to_asset,
        .amount = 300.0,
        .currency = "CNY",
        .transfer_date = "2026-10-17",
        .note = "Transfer 2",
        .ledger_id = 100
    };
    transfer_usecase_result_t res2 = {0};
    assert(transfer_usecase_create(pool, &cmd2, &res2) == 0);

    // 3. 查验 transfers 表，应有两条记录，且 from_tx_id 和 to_tx_id 均有效且互不相同
    char fid_str[32], tid_str[32], uid_str[32];
    snprintf(fid_str, sizeof(fid_str), "%lld", (long long)from_asset);
    snprintf(tid_str, sizeof(tid_str), "%lld", (long long)to_asset);
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);

    csilk_json_t* tr_rows = csilk_db_query_param_json(
        pool,
        "SELECT id, from_tx_id, to_tx_id, note FROM transfers WHERE user_id=? AND from_asset_id=? ORDER BY id ASC",
        (const char*[]){uid_str, fid_str, NULL});
    assert(tr_rows && csilk_json_array_size(tr_rows) == 2);
    const csilk_json_t* row1 = csilk_json_array_get(tr_rows, 0);
    const csilk_json_t* row2 = csilk_json_array_get(tr_rows, 1);

    int64_t tr1_id = db_get_int(row1, "id");
    int64_t tr1_from_tx = db_get_int(row1, "from_tx_id");
    int64_t tr2_id = db_get_int(row2, "id");
    int64_t tr2_from_tx = db_get_int(row2, "from_tx_id");

    assert(tr1_id > 0 && tr2_id > 0 && tr1_id != tr2_id);
    assert(tr1_from_tx > 0 && tr2_from_tx > 0 && tr1_from_tx != tr2_from_tx);
    csilk_json_free(tr_rows);

    // 4. 删除第一笔转账的主单 (tr1_from_tx)
    delete_tx_cmd_t d_cmd = {.user_id = user_id, .tx_id = tr1_from_tx};
    tx_usecase_result_t d_res = {0};
    assert(tx_usecase_delete(pool, &d_cmd, &d_res) == 0);

    // 5. 关键断言：第一笔转账必须已被删除，但第二笔转账必须依然完好存在！
    char tr1_str[32], tr2_str[32];
    snprintf(tr1_str, sizeof(tr1_str), "%lld", (long long)tr1_id);
    snprintf(tr2_str, sizeof(tr2_str), "%lld", (long long)tr2_id);

    csilk_json_t* chk1 = csilk_db_query_param_json(
        pool, "SELECT id FROM transfers WHERE id=?", (const char*[]){tr1_str, NULL});
    assert(!chk1 || csilk_json_array_size(chk1) == 0);
    if (chk1) csilk_json_free(chk1);

    csilk_json_t* chk2 = csilk_db_query_param_json(
        pool, "SELECT id, note FROM transfers WHERE id=?", (const char*[]){tr2_str, NULL});
    assert(chk2 && csilk_json_array_size(chk2) == 1);
    assert(strcmp(csilk_json_get_string(csilk_json_array_get(chk2, 0), "note"), "Transfer 2") == 0);
    csilk_json_free(chk2);

    // 6. 删除第二笔转账的主单 (tr2_from_tx)
    delete_tx_cmd_t d_cmd2 = {.user_id = user_id, .tx_id = tr2_from_tx};
    tx_usecase_result_t d_res2 = {0};
    assert(tx_usecase_delete(pool, &d_cmd2, &d_res2) == 0);

    // 7. 再次查验，第二笔转账也被清理干净，余额无损恢复
    csilk_json_t* chk2_after = csilk_db_query_param_json(
        pool, "SELECT id FROM transfers WHERE id=?", (const char*[]){tr2_str, NULL});
    assert(!chk2_after || csilk_json_array_size(chk2_after) == 0);
    if (chk2_after) csilk_json_free(chk2_after);

    printf("  ✅ Identical Duplicate Transfer Precise Deletion passed\n");
}
```

在 `main()` 中调用 `test_transfer_identical_duplicate_deletion(pool, user_id);`。

- [ ] **Step 2: 编译并单独运行 `test_domain_repositories`**

```bash
cmake --build backend/build --target test_domain_repositories && ./backend/build/test_domain_repositories
```
Expected: PASS，`✅ Identical Duplicate Transfer Precise Deletion passed`。

- [ ] **Step 3: 运行全量 33 个 CTest 单元测试套件**

```bash
cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: **100% tests passed, 0 tests failed out of 33**。

- [ ] **Step 4: 运行端到端集成测试**

```bash
cd backend && ./tests/test_link.sh && cd ..
```
Expected: 全部 143 处断言 100% 通过。

- [ ] **Step 5: 运行前端单元测试与构建检查**

```bash
npm --prefix frontend test && npm --prefix frontend run build
```
Expected: 105 tests passed, `vue-tsc` 0 errors, build success.

- [ ] **Step 6: 提交 Task 4 代码与实施计划**

```bash
git add backend/tests/unit/test_domain_repositories.c \
        docs/superpowers/plans/2026-10-17-transfer-explicit-transactions.md
git commit -m "test(transfer): 🧪 add unit test for identical duplicate transfer deletion and update plan"
```
