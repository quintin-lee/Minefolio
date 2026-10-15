# 资金交易、转账及统计逻辑修复实现计划 (Transactions, Transfers & Statistics Fix Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 Minefolio 在资金交易（多账本隔离、交易修改资产、手续费丢失）、转账（双写冲突、重复回滚、外键约束阻碍资产删除）以及统计报表（汇总卡片标签反转、多币种直加失真、内部转账虚增、盈亏算法口径不一）方面的全部核心逻辑缺陷。

**Architecture:** 严格遵循 DDD 四层架构与单向依赖。在基础设施与仓储层增加 `ledger_id` 与跨币种汇率标准化查询；在领域层统一持仓盈亏模型与交易流动方向；在应用层统一转账生命周期管理与回滚配对；在接口及前端层修正多账本上下文透传及统计卡片定义。

**Tech Stack:** C23 (GNU extensions), CMake, csilk framework, SQLite/PostgreSQL, Vue 3 + TypeScript (Element Plus, Pinia).

---

### Task 1: 修复资金交易的多账本隔离漏洞（写入与查询增加 ledger_id）

**Files:**
- Modify: `backend/src/core/ledger/ledger_types.h`
- Modify: `backend/src/core/ledger/ledger_engine.c:284-295`
- Modify: `backend/src/application/transaction/commands.h`
- Modify: `backend/src/application/transaction/usecases.c:114-140, 303-347, 350-405`
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:317-414`
- Modify: `backend/src/interfaces/http/controllers/transaction_controller.c:41-55, 176-240`
- Test: `backend/tests/unit/test_domain_transaction.c`

- [ ] **Step 1: 编写多账本隔离失败的单元测试**

在 `backend/tests/unit/test_domain_transaction.c` 中添加针对 `ledger_id` 过滤的断言测试：

```c
static void test_transaction_ledger_scoping(void) {
    csilk_db_pool_t* pool = db_get_pool();
    assert(pool != NULL);

    int64_t user_id = 1;
    int64_t ledger_a = 101;
    int64_t ledger_b = 102;
    int64_t asset_a = 1;

    // 插入账本 A 的交易
    create_tx_cmd_t cmd_a = {
        .user_id = user_id,
        .ledger_id = ledger_a,
        .asset_id = asset_a,
        .type = "deposit",
        .amount = 500.0,
        .date = "2026-10-15",
    };
    tx_usecase_result_t res_a = {0};
    int rc = tx_usecase_create(pool, &cmd_a, &res_a);
    assert(rc == 0 && res_a.code == 0);

    // 查询账本 B 的交易列表，预期查不到账本 A 的交易
    query_tx_filter_t filter_b = {
        .user_id = user_id,
        .ledger_id = ledger_b,
        .page = 1,
        .page_size = 20,
    };
    tx_usecase_result_t q_res = {0};
    rc = tx_usecase_query(pool, &filter_b, &q_res);
    assert(rc == 0);
    assert(q_res.total == 0);
}
```

- [ ] **Step 2: 运行测试以验证失败**

```bash
cd backend/build && ctest -R test_domain_transaction --output-on-failure
```
预期：编译错误或断言失败（`ledger_id` 未在 `ledger_tx_t` 或 `filter` 中声明/使用）。

- [ ] **Step 3: 修改数据结构与仓储层 SQL**

1. 在 `backend/src/core/ledger/ledger_types.h` 的 `ledger_tx_t` 结构中新增：
```c
int64_t ledger_id; /**< 归属账本 ID */
```

2. 在 `backend/src/core/ledger/ledger_engine.c` 的 `ledger_apply_tx` 中，将 `INSERT INTO transactions` 补充 `ledger_id` 列：
```c
        char lid_str[32];
        snprintf(lid_str, sizeof(lid_str), "%lld", (long long)tx->ledger_id);

        const char* ins_params[] = {uid_str,
                                    ast_str,
                                    last_str,
                                    cat_str,
                                    src_type,
                                    tx_type_str,
                                    dir,
                                    ldir,
                                    amt_buf,
                                    price_buf,
                                    qty_buf,
                                    fee_buf,
                                    cur_str,
                                    tx->tx_date ? tx->tx_date : now_fallback,
                                    tx->note ? tx->note : "",
                                    parent_tx_str,
                                    lid_str,
                                    ast_str,
                                    uid_str,
                                    NULL};

        csilk_json_t* ins_res = csilk_db_query_param_json(
            pool,
            "INSERT INTO transactions (user_id, asset_id, linked_asset_id, category_id, "
            "source_type, transaction_type, direction, linked_direction, "
            "amount, price_per_unit, quantity, fee, currency, transaction_date, note, "
            "parent_tx_id, ledger_id) "
            "VALUES (?, ?, CAST(NULLIF(?, '0') AS BIGINT), CAST(NULLIF(?, '0') AS BIGINT), ?, ?, "
            "?, NULLIF(?, ''), ?, ?, ?, ?, "
            "?, ?, ?, "
            "CAST(NULLIF(?, '') AS BIGINT), "
            "COALESCE(CAST(NULLIF(?, '0') AS BIGINT), (SELECT ledger_id FROM assets WHERE id = ?), "
            "(SELECT id FROM ledgers WHERE owner_id = ? AND is_default = 1))) "
            "RETURNING id",
            ins_params);
```

3. 在 `backend/src/application/transaction/commands.h` 中为 `create_tx_cmd_t` 和 `query_tx_filter_t` 添加 `int64_t ledger_id;`。

4. 在 `backend/src/infrastructure/repositories/transaction_repo_impl.c` 中为 `tx_list` 和 `tx_monthly` 增加 `int64_t ledger_id` 参数并拼接 SQL 过滤条件：
```c
    if (ledger_id > 0) {
        char lid[32];
        snprintf(lid, sizeof(lid), "%lld", (long long)ledger_id);
        AF("ledger_id", lid);
    }
```

5. 在 `backend/src/interfaces/http/controllers/transaction_controller.c` 中：
   - `api_transactions_create`: 设置 `cmd.ledger_id = ledger_id;`
   - `api_transactions_list`: 提取 `int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer"); filter.ledger_id = ledger_id;`
   - `api_transactions_monthly`: 提取 `int64_t ledger_id = ctx_ledger_id(c, user_id, "viewer");` 传入 `tx_usecase_monthly(db_get_pool(), user_id, ledger_id, month, &res);`

- [ ] **Step 4: 运行测试并验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_transaction --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交改动**

```bash
git add backend/src/core/ledger/ledger_types.h backend/src/core/ledger/ledger_engine.c backend/src/application/transaction/ commands.h usecases.c backend/src/infrastructure/repositories/transaction_repo_impl.h backend/src/infrastructure/repositories/transaction_repo_impl.c backend/src/interfaces/http/controllers/transaction_controller.c backend/tests/unit/test_domain_transaction.c
git commit -m "fix(transaction): support multi-ledger isolation and scope enforcement"
```

---

### Task 2: 修复交易更新时资产变更失效与字段置空缺陷

**Files:**
- Modify: `backend/src/application/transaction/usecases.c:146-258`
- Modify: `backend/src/core/ledger/ledger_engine.c:320-350`
- Test: `backend/tests/unit/test_domain_transaction.c`

- [ ] **Step 1: 编写更新交易更换资产的测试**

```c
static void test_transaction_update_change_asset(void) {
    csilk_db_pool_t* pool = db_get_pool();
    int64_t user_id = 1;
    int64_t asset_1 = 1; // 初始资产
    int64_t asset_2 = 2; // 目标新资产

    // 创建一笔买入交易到 asset_1
    create_tx_cmd_t cmd = {
        .user_id = user_id,
        .asset_id = asset_1,
        .type = "deposit",
        .amount = 1000.0,
        .date = "2026-10-15",
    };
    tx_usecase_result_t res = {0};
    int rc = tx_usecase_create(pool, &cmd, &res);
    assert(rc == 0 && res.created_id > 0);

    // 修改交易将 asset_id 变更为 asset_2
    update_tx_cmd_t u_cmd = {
        .user_id = user_id,
        .tx_id = res.created_id,
        .asset_id = asset_2,
        .amount = 1000.0,
        .type = "deposit",
        .date = "2026-10-15",
    };
    tx_usecase_result_t u_res = {0};
    rc = tx_usecase_update(pool, &u_cmd, &u_res);
    assert(rc == 0 && u_res.code == 0);

    // 验证交易记录上的 asset_id 已经更新为 asset_2
    mf_transaction_t tx_check = {0};
    rc = mf_tx_repo_find_by_id(pool, user_id, res.created_id, &tx_check);
    assert(rc == 0);
    assert(tx_check.asset_id == asset_2);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_domain_transaction --output-on-failure
```
预期：FAIL（`tx_check.asset_id == asset_2` 断言失败，因系统仍保留为 asset_1）。

- [ ] **Step 3: 实现更新用例与底层 SQL 对 asset_id 的变更支持**

1. 在 `backend/src/application/transaction/usecases.c` 的 `tx_usecase_update` 中：
```c
    int64_t target_asset_id = (cmd->asset_id > 0) ? cmd->asset_id : old_asset_id;
    int64_t target_linked_id = (cmd->linked_asset_id > 0) ? cmd->linked_asset_id : db_get_int(old_row, "linked_asset_id");
    int64_t target_cat_id = (cmd->category_id > 0) ? cmd->category_id : db_get_int(old_row, "category_id");

    /* 1. 先对原资产执行交易逆向回滚 */
    if (ledger_reverse_tx((csilk_db_pool_t*)pool, cmd->user_id, cmd->tx_id) != 0) { ... }

    /* 2. 组装新交易状态并应用到 target_asset_id */
    ledger_tx_t ltx = {
        .id = cmd->tx_id,
        .user_id = cmd->user_id,
        .asset_id = target_asset_id,
        .linked_asset_id = target_linked_id,
        .category_id = target_cat_id,
        ...
    };
```

2. 在 `backend/src/core/ledger/ledger_engine.c` 的 `UPDATE transactions` 语句中加入 `asset_id` 的更新：
```c
        csilk_json_t* upd_res = csilk_db_query_param_json(
            pool,
            "UPDATE transactions SET "
            "asset_id=?, transaction_type=?, direction=?, linked_direction=NULLIF(?, ''), "
            "amount=?, price_per_unit=?, quantity=?, fee=?, currency=?, "
            "transaction_date=?, note=?, category_id=CAST(NULLIF(?, '0') AS BIGINT), source_type=?, "
            "linked_asset_id=CAST(NULLIF(?, '0') AS BIGINT) "
            "WHERE id=? AND user_id=? RETURNING id",
            upd_params);
```

- [ ] **Step 4: 运行测试并验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_transaction --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交改动**

```bash
git add backend/src/application/transaction/usecases.c backend/src/core/ledger/ledger_engine.c backend/tests/unit/test_domain_transaction.c
git commit -m "fix(transaction): support changing asset_id and preserve linked fields on update"
```

---

### Task 3: 修复普通资金交易手续费（Fee）扣减丢失缺陷

**Files:**
- Modify: `backend/src/core/ledger/ledger_engine.c:385-434, 531-562`
- Test: `backend/tests/unit/test_ledger_engine.c`

- [ ] **Step 1: 编写普通资金交易手续费扣减单元测试**

在 `backend/tests/unit/test_ledger_engine.c` 中添加对取现及存入手续费扣减的断言：

```c
static void test_non_investment_tx_fee_deduction(void) {
    csilk_db_pool_t* pool = db_get_pool();
    int64_t user_id = 1;
    int64_t bank_account = 20;

    // 存入 1000 元，收取 5 元手续费
    currency_t cny = CURRENCY_CNY;
    money_t amt, fee;
    money_from_double(1000.0, cny, &amt);
    money_from_double(5.0, cny, &fee);

    ledger_tx_t tx = {
        .id = 0,
        .user_id = user_id,
        .asset_id = bank_account,
        .type = LEDGER_TX_DEPOSIT,
        .type_str = "deposit",
        .amount = amt,
        .fee = fee,
        .tx_date = "2026-10-15 10:00:00",
    };
    int rc = ledger_apply_tx(pool, &tx);
    assert(rc == 0);

    // 检查银行账户实际余额：应为 1000 - 5 = 995 元
    csilk_json_t* res = csilk_db_query_param_json(pool, "SELECT current_value FROM assets WHERE id = ?", (const char*[]){"20", NULL});
    assert(res && csilk_json_array_size(res) > 0);
    double val = db_get_num(csilk_json_array_get(res, 0), "current_value");
    csilk_json_free(res);
    assert(fabs(val - 995.0) < 0.001);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_ledger_engine --output-on-failure
```
预期：FAIL（`val` 仍为 1000.0，5 元手续费未扣除）。

- [ ] **Step 3: 优化 ledger_apply_tx 与 ledger_reverse_tx 中的手续费扣减**

在 `backend/src/core/ledger/ledger_engine.c` 中：
1. 取消手续费生成对 `is_investment_tx` 的强制绑定限制；对于非投资品交易，若 `fee > 0` 且无 `linked_asset_id`，直接从主资产 `tx->asset_id` 中执行手续费扣减（`balance_apply_delta_m(..., tx->asset_id, -fee, "transaction_fee", ...)`）并生成独立 `fee` 审计记录。
2. 在 `ledger_reverse_tx` 中，确保对该笔交易关联的手续费子行及直接手续费扣减执行对等回滚。

- [ ] **Step 4: 运行测试验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_ledger_engine --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/core/ledger/ledger_engine.c backend/tests/unit/test_ledger_engine.c
git commit -m "fix(ledger): deduct and track fee for non-investment transactions"
```

---

### Task 4: 修复转账双写冲突、级联重复回滚及资产删除死锁

**Files:**
- Create: `backend/sql/migrations/sqlite/V010__transfer_cascade_and_ledger.sql`
- Create: `backend/sql/migrations/postgres/V010__transfer_cascade_and_ledger.sql`
- Modify: `backend/src/infrastructure/repositories/transfer_repo_impl.c:34-125`
- Modify: `backend/src/application/transfer/usecases.c:48-128`
- Modify: `backend/src/core/ledger/ledger_engine.c:607-635`
- Test: `backend/tests/unit/test_domain_repositories.c`

- [ ] **Step 1: 编写转账成对回滚的单元测试**

在 `backend/tests/unit/test_domain_repositories.c` 中添加测试：

```c
static void test_transfer_delete_clean_rollback(void) {
    csilk_db_pool_t* pool = db_get_pool();
    int64_t user_id = 1;
    int64_t from_asset = 10;
    int64_t to_asset = 20;

    create_transfer_cmd_t cmd = {
        .user_id = user_id,
        .from_asset_id = from_asset,
        .to_asset_id = to_asset,
        .amount = 300.0,
        .currency = "CNY",
        .transfer_date = "2026-10-15",
    };
    transfer_usecase_result_t res = {0};
    int rc = transfer_usecase_create(pool, &cmd, &res);
    assert(rc == 0);

    // 查询该转账生成的 transfer_out 交易 ID 并执行删除
    csilk_json_t* txs = csilk_db_query_param_json(pool, "SELECT id FROM transactions WHERE transaction_type='transfer_out' AND asset_id=? ORDER BY id DESC LIMIT 1", (const char*[]){"10", NULL});
    assert(txs && csilk_json_array_size(txs) > 0);
    int64_t tx_out_id = db_get_int(csilk_json_array_get(txs, 0), "id");
    csilk_json_free(txs);

    delete_tx_cmd_t d_cmd = {.user_id = user_id, .tx_id = tx_out_id};
    tx_usecase_result_t d_res = {0};
    rc = tx_usecase_delete(pool, &d_cmd, &d_res);
    assert(rc == 0);

    // 验证：配对的 transfer_in 交易必须被级联清理，不得残留导致二次扣除
    csilk_json_t* in_txs = csilk_db_query_param_json(pool, "SELECT id FROM transactions WHERE transaction_type='transfer_in' AND asset_id=? AND amount=?", (const char*[]){"20", "300.00", NULL});
    assert(!in_txs || csilk_json_array_size(in_txs) == 0);
    if (in_txs) csilk_json_free(in_txs);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_domain_repositories --output-on-failure
```
预期：FAIL（配对的 `transfer_in` 仍然存在，未联动清理）。

- [ ] **Step 3: 编写 V010 迁移脚本与配对关联逻辑**

1. 创建 `backend/sql/migrations/sqlite/V010__transfer_cascade_and_ledger.sql`：
   * 将 `transfers` 表的外键定义更新为 `ON DELETE CASCADE`；
   * 为 `transfers` 表增加 `ledger_id` 列。

2. 创建 `backend/sql/migrations/postgres/V010__transfer_cascade_and_ledger.sql`（对应 PG DDL）。

3. 在 `transfer_usecase_create` 插入 `transfer_out` 与 `transfer_in` 时，将两条交易的 `parent_tx_id` 互相关联，或通过统一的 `transfer_id` 标识关联。

4. 在 `tx_usecase_delete` 中，若删除的交易为 `transfer_out` 或 `transfer_in`，一并删除其配对的另一半交易（不再单独触发多余的重复回滚算子）并清理 `transfers` 表中对应记录。

- [ ] **Step 4: 运行测试并验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_repositories --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交改动**

```bash
git add backend/sql/migrations/ backend/src/infrastructure/repositories/transfer_repo_impl.c backend/src/application/transfer/usecases.c backend/src/application/transaction/usecases.c backend/src/core/ledger/ledger_engine.c backend/tests/unit/test_domain_repositories.c
git commit -m "fix(transfer): pair transfer transactions and prevent duplicate balance rollback"
```

---

### Task 5: 修复前端交易汇总卡片标签颠倒与内部转账虚增流水

**Files:**
- Modify: `backend/src/infrastructure/repositories/transaction_repo_impl.c:403-414`
- Modify: `frontend/src/views/Transactions.vue:28-37`
- Test: `backend/tests/unit/test_domain_transaction.c`

- [ ] **Step 1: 编写 tx_monthly 排除内部转账的单元测试**

在 `backend/tests/unit/test_domain_transaction.c` 中：

```c
static void test_tx_monthly_excludes_transfers(void) {
    csilk_db_pool_t* pool = db_get_pool();
    int64_t user_id = 1;
    int64_t ledger_id = 101;

    // 录入一笔买入 500 元、一笔内部转账 2000 元
    // 预期当月 inflows=0, outflows=500, total_volume=500，2000 元纯转账不计入外部收支
    csilk_json_t* res = tx_monthly(pool, user_id, ledger_id, "2026-10%");
    assert(res && csilk_json_array_size(res) > 0);
    const csilk_json_t* row = csilk_json_array_get(res, 0);
    double out_val = db_get_num(row, "outflows");
    csilk_json_free(res);
    assert(fabs(out_val - 500.0) < 0.001);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_domain_transaction --output-on-failure
```
预期：FAIL（`outflows` 为 2500，计入了转账的 2000）。

- [ ] **Step 3: 优化 tx_monthly SQL 并修正前端卡片标签**

1. 修改 `backend/src/infrastructure/repositories/transaction_repo_impl.c` 中 `tx_monthly` 的 SQL：
```c
        "SELECT COALESCE(SUM(amount),0) AS total_volume,"
        "COALESCE(SUM(CASE WHEN direction='in' THEN amount ELSE 0 END),0) AS inflows,"
        "COALESCE(SUM(CASE WHEN direction='out' THEN amount ELSE 0 END),0) AS outflows,"
        "COUNT(*) AS count FROM transactions WHERE user_id=? AND ledger_id=? "
        "AND transaction_type NOT IN ('transfer_in', 'transfer_out') "
        "AND CAST(transaction_date AS TEXT) LIKE ?"
```

2. 修改 `frontend/src/views/Transactions.vue`（第 28-37 行）：
```vue
      <el-col :xs="12" :sm="6">
        <div class="summary-card-with-bar bar-red">
          <SummaryCard label="卖出/存入/收益" :value="'+' + formatCurrency(monthlyInflows)" type="income" extraClass="font-mono" />
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="summary-card-with-bar bar-green">
          <SummaryCard label="买入/取出/支出" :value="'-' + formatCurrency(monthlyOutflows)" type="expense" extraClass="font-mono" />
        </div>
      </el-col>
```

- [ ] **Step 4: 运行测试与前端构建验证**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_transaction --output-on-failure
npm --prefix frontend run build
```
预期：PASS，vue-tsc 编译零错误。

- [ ] **Step 5: 提交改动**

```bash
git add backend/src/infrastructure/repositories/transaction_repo_impl.c frontend/src/views/Transactions.vue backend/tests/unit/test_domain_transaction.c
git commit -m "fix(transaction): correct monthly summary labels and exclude internal transfers from cashflow stats"
```

---

### Task 6: 统一日常收支与报表的多币种汇率折算口径

**Files:**
- Modify: `backend/src/infrastructure/repositories/daily_expense_repo_impl.c:108-123`
- Modify: `backend/src/application/daily_expense/usecases.c:130-170`
- Test: `backend/tests/unit/test_domain_multi_currency.c`

- [ ] **Step 1: 编写多币种账单月度汇总换算的单元测试**

在 `backend/tests/unit/test_domain_multi_currency.c` 中添加测试：

```c
static void test_daily_expense_multi_currency_totals(void) {
    csilk_db_pool_t* pool = db_get_pool();
    int64_t user_id = 1;
    int64_t ledger_id = 101;

    // 录入一笔 100 CNY 支出和一笔 10 USD 支出 (汇率 7.20)
    // 预期合计支出折算为 100 + 72 = 172 CNY，而不是 110
    create_daily_expense_cmd_t cmd1 = {
        .user_id = user_id, .ledger_id = ledger_id, .asset_id = 1, .category_id = 1,
        .amount = 100.0, .currency = "CNY", .type = "expense", .date = "2026-10-15"
    };
    create_daily_expense_cmd_t cmd2 = {
        .user_id = user_id, .ledger_id = ledger_id, .asset_id = 2, .category_id = 1,
        .amount = 10.0, .currency = "USD", .type = "expense", .date = "2026-10-15"
    };
    daily_expense_usecase_create(pool, &cmd1, NULL);
    daily_expense_usecase_create(pool, &cmd2, NULL);

    csilk_json_t *totals = NULL, *cats = NULL, *tags = NULL, *daily = NULL;
    int rc = daily_expense_usecase_monthly(pool, user_id, ledger_id, "2026-10", &totals, &cats, &tags, &daily);
    assert(rc == 0 && totals != NULL);
    double exp = db_get_num(totals, "total_expense");
    csilk_json_free(totals);
    if (cats) csilk_json_free(cats);
    if (tags) csilk_json_free(tags);
    if (daily) csilk_json_free(daily);

    assert(fabs(exp - 172.0) < 0.1);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_domain_multi_currency --output-on-failure
```
预期：FAIL（`exp` 输出为 110.0，未进行汇率换算）。

- [ ] **Step 3: 在应用层根据币种逐条折算求和**

修改 `backend/src/application/daily_expense/usecases.c` 的 `daily_expense_usecase_monthly`：
拉取包含 `amount, currency` 的明细数据，使用 `exchange_rate_get_to_cny(cur)` 转换为基准币种后聚合成 `total_income` 与 `total_expense`，与 `report_expense_service.c` 保持一致逻辑。

- [ ] **Step 4: 运行测试并验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_multi_currency --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交改动**

```bash
git add backend/src/application/daily_expense/usecases.c backend/src/infrastructure/repositories/daily_expense_repo_impl.c backend/tests/unit/test_domain_multi_currency.c
git commit -m "fix(daily_expense): convert currencies to base currency in monthly totals calculation"
```

---

### Task 7: 统一持仓分析（Holdings）与绩效报表（Performance）盈亏计算口径

**Files:**
- Modify: `backend/src/domain/portfolio/rules.c:60-105`
- Test: `backend/tests/unit/test_domain_portfolio.c`

- [ ] **Step 1: 编写持仓盈亏计入手续费与分红的单元测试**

在 `backend/tests/unit/test_domain_portfolio.c` 中：

```c
static void test_holdings_pnl_includes_fees_and_dividends(void) {
    // 买入 100 股 @ 10 元，手续费 5 元 -> 成本基础 1005 元
    // 卖出 100 股 @ 12 元，手续费 8 元 -> 净变现 1192 元
    // 已实现盈亏应为 1192 - 1005 = 187 元
    mf_holding_item_t item = {
        .asset_id = 1,
        .currency = CURRENCY_CNY,
        .quantity = quantity_zero(),
        .cost_basis = money_zero(CURRENCY_CNY),
    };
    mf_portfolio_trade_event_t events[2] = {
        {
            .asset_id = 1,
            .type = "buy",
            .quantity = quantity_from_int(100),
            .amount = money_from_amount(1000, CURRENCY_CNY),
            .fee = money_from_amount(5, CURRENCY_CNY),
        },
        {
            .asset_id = 1,
            .type = "sell",
            .quantity = quantity_from_int(100),
            .amount = money_from_amount(1200, CURRENCY_CNY),
            .fee = money_from_amount(8, CURRENCY_CNY),
        }
    };
    int rc = mf_portfolio_rule_apply_trade_events(&item, 1, events, 2);
    assert(rc == 0);
    double pnl = money_to_double(item.realized_pnl);
    assert(fabs(pnl - 187.0) < 0.001);
}
```

- [ ] **Step 2: 运行测试以确认失败**

```bash
cd backend/build && ctest -R test_domain_portfolio --output-on-failure
```
预期：FAIL（`pnl` 输出为 200.0，手续费被忽略）。

- [ ] **Step 3: 统一 mf_portfolio_rule_apply_trade_events 算法模型**

在 `backend/src/domain/portfolio/rules.c` 中：
1. `buy` 事件：成本池 `cost_for_pnl` 累加 `ev->amount + ev->fee`；
2. `sell` 事件：变现收益为 `ev->amount - ev->fee`，扣除对应份额成本后计入已实现盈亏；
3. `dividend` 与 `interest` 事件：直接作为正向已实现收益累加到 `acc->realized`。

- [ ] **Step 4: 运行测试并验证通过**

```bash
cd backend/build && cmake --build . --parallel && ctest -R test_domain_portfolio --output-on-failure
```
预期：PASS。

- [ ] **Step 5: 提交改动**

```bash
git add backend/src/domain/portfolio/rules.c backend/tests/unit/test_domain_portfolio.c
git commit -m "fix(portfolio): align holdings pnl calculation with fees and dividends"
```

---

### Task 8: 全面回归与集成验证

**Files:**
- Test: `backend/tests/test_link.sh`
- Test: `frontend/`

- [ ] **Step 1: 运行后端全部 32 个 CTest 单元测试套件**

```bash
cd backend/build && ctest --output-on-failure
```
预期：32/32 suites 100% PASS。

- [ ] **Step 2: 运行核心端到端集成测试脚本**

```bash
cd backend && ./tests/test_link.sh
```
预期：全部用例校验通过，断言无一失败。

- [ ] **Step 3: 运行前端类型检查与生产构建**

```bash
npm --prefix frontend run build && npm --prefix frontend run build:mobile
```
预期：`vue-tsc -b` 严格模式零错误，Vite 构建输出成功。

- [ ] **Step 4: 最终提交**

```bash
git commit --allow-empty -m "chore: complete verification for transaction, transfer, and statistics fixes"
```
