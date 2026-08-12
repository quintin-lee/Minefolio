# 投资交易关联资金账户双向联动设计规范

## 1. 概述
在投资交易（买入基金/股票、卖出赎回、追加本金等）场景中，交易不仅改变投资资产本身的持仓与市值，还涉及实际出资/收款的资金账户（如银行卡、支付宝余额）。本设计引入 `linked_asset_id`（关联资金账户），实现买入自动扣款、卖出自动资金回流的双向余额联动与审计追踪。

## 2. 数据库变更

### 2.1 表结构调整 (`backend/sql/migration.sql`)
`transactions` 表新增可选外键字段：
```sql
ALTER TABLE transactions ADD COLUMN linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL;
```

### 2.2 存量库平滑迁移 (`backend/src/common/db.c`)
在 `db_run_migrations` 中使用 PRAGMA 列检测，若存量库缺少 `linked_asset_id` 自动追加列：
```c
int has_linked_asset = 0;
csilk_json_t* cols = csilk_db_query_json(pool, "PRAGMA table_info(transactions)");
// 遍历检测 linked_asset_id
if (!has_linked_asset) {
    csilk_db_exec(pool, "ALTER TABLE transactions ADD COLUMN linked_asset_id INTEGER REFERENCES assets(id) ON DELETE SET NULL");
}
```

## 3. 后端业务逻辑 (`backend/src/transactions.c`)

### 3.1 余额联动计算规则 (`linked_delta`)
对于交易记录，设目标资产变动为 `target_delta`，关联资金账户变动为 `linked_delta`：

| 交易类型 (`transaction_type`) | 目标资产 (`asset_id`) 变动 (`target_delta`) | 关联资金账户 (`linked_asset_id`) 变动 (`linked_delta`) |
| :--- | :--- | :--- |
| `buy` (买入/申购) | `+amount` | `-amount` (自动从银行卡扣款) |
| `deposit` (存款/本金追加) | `+amount` | `-amount` (自动从银行卡扣除) |
| `sell` (卖出/赎回) | `-amount` | `+amount` (自动回流到银行卡) |
| `withdrawal` (取出) | `-amount` | `+amount` (资金提现到银行卡) |
| `income` (分红/收益) | `+amount` | `+amount` (若指定关联账户则到账) |
| `fee` (手续费) / `loss` (亏损) | `-amount` | `-amount` (若指定关联账户则扣除) |

### 3.2 接口处理规则
- **`transactions_list`**：
  - LEFT JOIN 第二次 `assets la ON t.linked_asset_id = la.id` 提取 `la.name as linked_asset_name`。
  - 返回对象包含 `linked_asset_id` 与 `linked_asset_name`。
- **`transactions_create`**：
  - 解析 `linked_asset_id`，若有效则校验其归属于当前用户且不能等于 `asset_id`。
  - 在 `BEGIN TRANSACTION` 内：
    1. 插入 `transactions` 记录；
    2. 调用 `balance_apply_delta` 应用 `target_delta`；
    3. 若 `linked_asset_id > 0`，调用 `balance_apply_delta` 应用 `linked_delta`（`source_type="transaction_linked"`）；
    4. 成功后 `COMMIT`，任何失败则 `ROLLBACK`。
- **`transactions_update`**：
  - 读取旧记录的 `old_asset_id`, `old_linked_asset_id`, `old_amount`, `old_type` 等。
  - 计算目标资产与关联资金账户的新旧差量，原子同步更新两边资产余额与审计日志。
- **`transactions_delete`**：
  - 反转目标资产与关联资金账户的旧变动量。

## 4. 前端接口与页面更新 (`frontend/src/views/Transactions.vue`)

### 4.1 TypeScript 类型 (`frontend/src/types/index.ts`)
```ts
export interface Transaction {
  id: number
  user_id: number
  asset_id: number
  linked_asset_id?: number | null
  asset_name?: string
  linked_asset_name?: string
  // ... 其他字段
}
```

### 4.2 交易列表与表单 UI 调整
- **新增/编辑对话框 (`.premium-dialog`)**：
  - 增加“关联资金账户”下拉单选框：
    ```html
    <el-form-item label="资金账户">
      <el-select v-model="form.linked_asset_id" placeholder="选择支付/扣款/回流账户（可选）" clearable style="width: 100%" filterable>
        <el-option v-for="a in allAssets" :key="a.id" :label="a.name" :value="Number(a.id)" />
      </el-select>
    </el-form-item>
    ```
- **交易历史数据表格 (`.premium-table`)**：
  - 增加“关联资金账户”数据列，若存在显示 `linked_asset_name`（带轻量灰蓝色 Tag），不存在显示 `-`。

## 5. 验证标准
- 前端 `npm --prefix frontend run build` 无 TypeScript/Vite 编译错误。
- 后端 `cmake --build backend/build` 编译无错。
- 自动化测试 `cd backend && ./tests/test_link.sh` PASS 增加对划转及交易关联资金账户扣款/回流的校验。
