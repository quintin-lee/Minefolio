# Minefolio 账本状态与资产所有权审计报告 (Ledger & Asset Ownership Audit)

## 1. 核心设计原则 (Core Principle)

* **Transaction / Event 是唯一金融事实 (Single Source of Truth)**
* **资产余额 (`current_value`)、持仓份额 (`quantity`)、成本基础 (`cost_basis`)、单价净值 (`net_value`) 及实现/浮动盈亏 (`realized_pnl` / `unrealized_pnl`) 是派生物化状态 (Derived / Materialized State)**
* **所有状态变更必须由统一的 Ledger Engine 执行，杜绝任何外部服务直接旁路修改资产状态**

---

## 2. 状态变动点全量扫描与所有权归属矩阵

| 模块 / 文件 | 操作语义 | 原有直接修改方式 | Ledger Engine 统一接管方案 |
| :--- | :--- | :--- | :--- |
| `transaction_write.c` | 交易创建 (`transactions_create`) | 手工调用 `apply_position` + 手工生成 fee 记录 + 手工调用 `balance_apply_delta` 目标资产与资金账户 | 统一由 `ledger_apply_tx(pool, &tx)` 处理：原子完成持仓、资金扣减、手续费生成与审计落库 |
| `transaction_write.c` | 交易修改 (`transactions_update`) | 分支判断投资/非投资类，手工计算反向 delta，级联查询并删除 fee，再重新调用 apply | 统一由 `ledger_reverse_tx(pool, uid, old_id)` + `ledger_apply_tx(pool, &new_tx)` 处理 |
| `transaction_write.c` | 交易删除 (`transactions_delete`) | 手工查询子手续费并回滚余额，回滚主交易持仓/余额，最后 DELETE | 统一由 `ledger_reverse_tx(pool, uid, tx_id)` 并在同事务中物理/逻辑删除 |
| `daily_expense_write.c` | 收支记账 (`create / update / delete`) | 直接调用 `balance_apply_delta` | 统一通过 `ledger_apply_expense` / `ledger_reverse_expense` 统一状态机处理 |
| `transfer_service.c` | 账户转账 (`create / update / delete`) | 直接调用两次 `balance_apply_delta` 扣出/入账 | 统一通过 `ledger_apply_transfer` / `ledger_reverse_transfer` 处理 |
| `dca_service.c` | 定投一键买入 (`confirm_execution`) | 插入 transaction 表后手工调用 `apply_position` 与 `balance_apply_delta` | 统一调用 `ledger_apply_tx` 处理 |
| `cashflow_service.c` | 现金流日历确认 (`confirm`) | 插入 transaction 表后手工调用 `balance_apply_delta` | 统一调用 `ledger_apply_tx` 处理 |
| `import_service.c` | 批量导入 CSV | 逐行调用 `apply_position` 与 `balance_apply_delta` | 统一走 `ledger_apply_tx` 与 `ledger_apply_expense` |
| `asset_service.c` | 资产手动修正净值/份额/成本 | 直接执行 SQL `UPDATE assets SET quantity=..., cost_basis=...` | 若用户手动修正，自动生成 `ADJUSTMENT` 交易事实落库，触发账本状态机物化 |

---

## 3. 金融语义状态流转规则 (Transaction Semantics)

1. **BUY (投资买入)**:
   * 目标资产 (Target Asset): 持仓增加 $+quantity$，成本基础增加 $+amount (+fee)$，净值更新为 $price$；
   * 资金账户 (Funding Asset): 扣除现金 $-(amount + fee)$；
   * 手续费 (Fee): 若 $fee > 0$，自动生成关联父交易的 `fee` 子交易记录。
2. **SELL (投资卖出)**:
   * 目标资产 (Target Asset): 持仓减少 $-quantity$，成本基础按持仓比例减少 $\frac{sell\_quantity}{current\_quantity} \times current\_cost\_basis$；
   * 资金账户 (Funding Asset): 增加现金 $+(amount - fee)$；
   * 损益 (Realized PnL): 产生实现盈亏 $(amount - fee) - cost\_reduction$。
3. **DEPOSIT / INCOME (资金存入/收入)**:
   * 资产增加 $+amount$（若是负债账户如信用卡还款，则欠款减少 $-amount$）。
4. **WITHDRAW / EXPENSE (资金提取/支出)**:
   * 资产减少 $-amount$（若是负债账户如信用卡刷卡，则欠款增加 $+amount$）。
5. **TRANSFER (资金调拨)**:
   * 转出账户扣减 $-amount$，转入账户增加 $+amount$（各自遵循自身资产类型的借贷方向语义）。
6. **DIVIDEND (投资分红/派息)**:
   * 现金分红增加资金账户 $+amount$，或者选择摊薄目标资产持仓成本 $-amount$。
7. **INTEREST / FEE / TAX (利息/费用/税金)**:
   * 针对对应账户执行精确资金增减与流水关联。
8. **ADJUSTMENT (账面核校调整)**:
   * 记录盘点调整事实，将派生状态与实际盘点值对齐。

---

## 4. Rebuild 状态重算数学恒等式 (Event Sourcing Rebuild)

对任意资产 $A$，当清空其物化字段并按时间戳顺序 $t_1, t_2, \dots, t_n$ 重新回放所有交易：
$$\text{Rebuild}(\{T_i\}_{i=1}^n) \equiv \text{Current State}$$
* `rebuild_position(asset_id)`: 遍历其全部交易，重新计算加权平均持仓、成本及最新净值。
* `rebuild_account(account_id)`: 遍历其全部交易、日常收支及出入转账，重新汇总当前现金/负债余额。
* `rebuild_portfolio(user_id)`: 全局重算用户的所有资产账户与投资标的。
