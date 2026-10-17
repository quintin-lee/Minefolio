# 转账辅助表显式双向外键与 V014 数据库迁移设计方案

- **日期**：2026-10-17
- **状态**：Approved (已评审通过)
- **目标范围**：`transfers` 辅助表显式双向外键 (`from_tx_id`, `to_tx_id`)、`V014` 数据库迁移 (SQLite + PostgreSQL)、转账写入时序重排、交易确定性删除、同金额并发划转防误删单元测试

---

## 1. 背景与问题定位

在 Minefolio 系统中，转账操作通过两笔配对交易记录以及一条辅助流水记录构成：
* `transactions` 表记录 `transfer_out` 交易（转出账户扣款，主单）。
* `transactions` 表记录 `transfer_in` 交易（转入账户增款，子单，`parent_tx_id` 权威指向 `transfer_out`）。
* `transfers` 表记录划转元数据（`from_asset_id`, `to_asset_id`, `amount`, `transfer_date`, `note`, `ledger_id`）。

### 历史缺陷：
`transfers` 辅助表建立之初未保留指向 `transactions` 的外键字段（仅有 `from_asset_id` 与 `to_asset_id`）。在删除转账交易（`application/transaction/usecases.c`）时，系统被迫采用“启发式属性匹配”进行清理：
```sql
DELETE FROM transfers WHERE id IN (
  SELECT id FROM transfers WHERE user_id=? AND from_asset_id=? AND to_asset_id=? AND amount=?
  ORDER BY id DESC LIMIT 1
)
```
**隐患与缺陷**：
当用户在短时间内进行同账户、同金额的多次划转（例如：微信 -> 招行 分别转账 500 元两次），若用户删除第一笔转账交易，上述逻辑会因 `ORDER BY id DESC LIMIT 1` **错误删除第二笔转账对应的 `transfers` 记录**，而第一笔转账的 `transfers` 记录却残留下来。这会导致数据状态不一致，属于典型的启发式推断缺陷。

---

## 2. 详细设计

### 2.1 数据库 Schema 与 V014 迁移脚本

在 `transfers` 表中增加两个显式外键列，并配置级联删除约束：
* `from_tx_id`: `BIGINT REFERENCES transactions(id) ON DELETE CASCADE`（指向 `transfer_out`）
* `to_tx_id`: `BIGINT REFERENCES transactions(id) ON DELETE CASCADE`（指向 `transfer_in`）

#### 1. SQLite 迁移脚本 (`backend/sql/migrations/sqlite/V014__transfer_explicit_transactions.sql`)
采用无锁重建表模式，保证约束与级联在所有 SQLite 环境下 100% 可靠：
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

#### 2. PostgreSQL 迁移脚本 (`backend/sql/migrations/postgres/V014__transfer_explicit_transactions.sql`)
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

#### 3. 同步全新安装基线
* `backend/sql/migration.sql`
* `backend/sql/migration_postgres.sql`

---

### 2.2 实体与仓储层扩展

#### 1. 实体定义 (`backend/src/domain/transfer/entity.h`)
在 `mf_transfer_t` 结构体中扩展 `from_tx_id` 与 `to_tx_id`：
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

#### 2. 仓储层实现 (`backend/src/infrastructure/repositories/transfer_repo_impl.c`)
在 `mf_transfer_repo_insert` 中写入 `from_tx_id` 与 `to_tx_id`：
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

---

### 2.3 业务用例时序倒置 (`backend/src/application/transfer/usecases.c`)

在 `transfer_usecase_create()` 中，重排数据库事务内的创建时序：
1. **先写 `transfer_out` 交易**：获取 `out_tx_id`。
2. **后写 `transfer_in` 交易**：以 `out_tx_id` 为 `parent_tx_id` 写入，获取 `in_tx_id`。
3. **写入 `transfers` 辅助流水**：装配 `transfer.from_tx_id = out_tx_id`，`transfer.to_tx_id = in_tx_id`，写入 `transfers` 表获得 `transfer_id`。
4. **总账引擎更新余额**：调用 `ledger_apply_transfer`。
5. **提交事务**：`COMMIT`。

若其中任意一步失败，立即执行 `ROLLBACK`，天然保障外键完整性与原子性。

---

### 2.4 交易删除用例确定性清理 (`backend/src/application/transaction/usecases.c`)

在 `tx_usecase_delete()` 中，无论用户删除 `transfer_out`（`tid_str`）还是 `transfer_in`（`tid_str` 或 `maid_str`）：
1. **首选显式外键清理**：
   ```sql
   DELETE FROM transfers WHERE user_id=? AND (from_tx_id=? OR to_tx_id=?)
   ```
2. **兼容历史遗留行**：
   若受影响记录为 0 或属于无外键的历史数据，安全降级至原有的属性匹配兜底：
   ```sql
   DELETE FROM transfers WHERE id IN (
     SELECT id FROM transfers WHERE user_id=? AND from_asset_id=? AND to_asset_id=? AND amount=?
     ORDER BY id DESC LIMIT 1
   )
   ```

---

## 3. 验证计划

1. **编译构建**：
   `cmake -B backend/build -G "Unix Makefiles" && cmake --build backend/build --parallel`
2. **单元测试与同金额并发划转防误删验证**：
   * 更新 `test_migration_engine.c`（发现版本数检查断言 `count >= 14`）。
   * 在 `test_domain_repositories.c` 中添加同账户同金额两笔划转精准删除验证用例。
   * 运行全量 33 个 CTest 单元测试，确保 100% 通过。
3. **端到端集成测试**：
   * 执行 `./backend/tests/test_link.sh`，保证 143 处断言全部通过。
4. **前端编译与测试**：
   * `npm --prefix frontend test`（105 个单元测试全部通过）。
   * `npm --prefix frontend run build`（`vue-tsc` 0 报错）。
