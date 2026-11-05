# 账本与金融核心重塑及 AI 运行时工作流升级设计方案

- **日期**：2026-11-02
- **状态**：Completed & Verified（四大里程碑全量交付与验证完毕，已合并至 master）
- **目标路径**：`docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md`

---

## 1. 意图与背景 (Intent and The Why)

Minefolio 作为一个基于 C23 + csilk 构建的自托管个人资产与投资追踪系统，目前已完成了基础 DDD 架构分层、固定精度定点金融算术（128-bit decimal）、事务作用域保存点（`db_tx_scope_t`）以及初步的 AI 财务对话与工作流。

随着用户真实财务数据量的增长（如数万笔历史交易、长达数年的持仓历史）以及 AI 智能协同深度的拓展，当前系统暴露出两类深层次架构瓶颈：

1. **账本与金融核心的瓶颈**：
   - **重算性能瓶颈**：现有的事件溯源重放机制（`ledger_rebuild_position` / `ledger_rebuild_portfolio`）每次均从最早交易从零顺序回放（$O(N)$ 复杂度），缺乏阶段快照（Checkpointing）切片。
   - **历史外币估值失真**：历史时点的多币种资产市值与净资产趋势折算，直接使用了当前最新汇率，而非历史发生日的实际汇率。
   - **成本结转模式单一**：仅支持移动加权平均法（Weighted Average Cost），无法满足专业投资者或税务核算所需的先进先出（FIFO）及特定批次（Specific Tax Lots）追踪。

2. **AI 运行时与工作流体系的瓶颈**：
   - **工具上下文严重膨胀**：`get_assets`、`get_transactions` 等只读工具返回的全量数据库字段直接填入对话上下文，单次调用即消耗大量 Token，迅速耗尽记忆窗口并拖慢推理。
   - **工作流缺乏分支与容错补偿**：内置的 13 个财务工作流为单向线性链式执行，缺少条件分支跳转（如按资产体量分流）与中间失败补偿（Saga 回退）机制。
   - **缺乏复合动作批量协议**：AI 拟定记账/转账仅支持单笔生成与单笔确认，无法针对复合账单实现原子批量签名与一次性入账。

为实现系统的终极完备性，同时严控变更对现有 45 个单元测试和 174 个端到端集成测试的破坏风险，本设计确定采用**全量重塑分阶段演进（Incremental Multi-Milestone Migration）**路线，将目标方案三拆解为四个高度解耦、向后兼容、可细颗粒度逐步交付与验证的里程碑。

---

## 2. 目标、非目标与反目标 (Goals, Non-Goals, Anti-Goals)

### 2.1 目标 (Goals)
1. **账本增量快照检查点**：建立月度/季度检查点快照表 `ledger_snapshots`，结合历史交易变动时的级联失效机制，将历史重算耗时降低至常数级增量回放（$O(\Delta)$），同时保持重算与原状态的绝对位一致（Bit-identical consistency）。
2. **时点汇率切片对齐**：交易和快照落库时固化当时的生效外汇汇率，确保历史任何时间点的持仓折算、盈亏重溯严格符合当时真实市场行情。
3. **资产级多策略成本结转**：抽象统一的成本结转策略接口，在保持移动加权平均默认行为的同时，支持资产级的 FIFO（先进先出）持仓批次结转。
4. **AI 上下文精简投影**：工具返回模型时实施语义投影（Projection Filter），去除物理外键、内部时间戳等冗余元数据，压缩 70% 以上的上下文 Token 占用。
5. **复合操作原子批量协议**：支持多笔建议记账/转账的批量拟定（`propose_batch_actions`）与统一签名确认端点（`confirm_batch`），在单一事务内原子落地。
6. **Saga 补偿型动态工作流引擎**：工作流支持节点前置条件判断（Condition Branching）、失败自动补偿（Rollback Handlers）与内存级流式状态暂存。

### 2.2 非目标 (Non-Goals)
- 不废弃现有的单笔记账与转账接口，保持 API 与 Vue 3 前端完全向下兼容。
- 不引入外部重型分布式工作流引擎或额外的外部中间件依赖（依然保持纯 C23 + csilk 零外部依赖自包含架构）。
- 不在现有阶段推行复杂的跨年税务抵扣清洗规则（Wash Sale），仅提供标准的 FIFO 批次追踪基础能力。

### 2.3 反目标 (Anti-Goals)
- **绝对禁止大爆炸式全量重写**：严禁在一个 PR/Commit 中将底层账本算法和上层工作流全部推倒重写，导致测试断言大面积红灯无法定位根因。
- **严禁破坏定点数财务精度**：任何批次计算、汇率折算与快照存储，严禁回退到 `double` 裸浮点数，必须全程使用 `money_t`、`price_t`、`quantity_t` 和 128 位定点运算。
- **严禁绕过确认策略**：AI 批量入账绝不允许绕过已建成的 HMAC 令牌验签、Nonce 防重放和策略权限审核。

---

## 3. 核心业务边界与架构裁定 (Architectural Decisions)

经架构评审与用户确认，本方案落地以下 3 项基础裁定：

1. **成本结转法配置与锁定规则**：
   - 成本法（加权平均 vs FIFO）存储在资产维度（`assets.cost_method`），每个投资标的独立指定（默认为 `weighted_average`）。
   - **锁定规则**：一旦资产发生第一笔交易，`cost_method` 字段即被冻结，禁止中途随意修改，避免因历史批次重构导致收益曲率与税务报表产生前后不一致。
2. **AI 批量操作的交互形式与支持范围**：
   - AI 拟定的批量操作卡片在前端呈现为**只读核对清单**，用户进行整批一次性确认签署（若需调整金额或分类，通过向 AI 对话指出重新拟定生成新令牌）。
   - **支持范围**：锁定在日常收支（`daily_expense`）与资产转账（`transfer`）两大高频流水场景，暂不引入复杂的投资标的交易。
3. **Saga 工作流的执行定位**：
   - 工作流引擎定位为**单次流式请求生命周期内的内存状态机**，专门负责在多步骤取数、分析、汇总异常时提供轻量步骤重试与临时状态回滚，无需引入跨进程持久化任务表。

---

## 4. 详细架构设计与任务拆解 (Detailed Breakdown by Milestones)

```
+-----------------------------------------------------------------------------------+
|                           Unified Personal Finance Platform                       |
+-----------------------------------------------------------------------------------+
|  [Milestone 3 & 4] AI Runtime & Workflow Layer                                    |
|   ├── Compact Prompt Projection  (Token Reduction 70%)                            |
|   ├── Batch HMAC Draft Actions   (Multi-action Atomic Confirm)                    |
|   └── Saga Workflow Engine       (Dynamic Conditions, Retry, Rollback Compensation)|
+-----------------------------------------------------------------------------------+
|  [Milestone 1 & 2] Financial & Ledger Core Layer                                  |
|   ├── Ledger Checkpointing       (Monthly Snapshots & O(Δ) Incremental Replay)     |
|   ├── Point-in-Time FX Rates     (Historical Currency Snapshotting)               |
|   └── Multi-Method Cost Basis    (Weighted Average + FIFO Lots Engine)            |
+-----------------------------------------------------------------------------------+
|  Storage & Transport Layer (SQLite / PostgreSQL / csilk REST / JSON-RPC MCP)      |
+-----------------------------------------------------------------------------------+
```

---

### 里程碑 1：账本增量快照检查点与历史时点汇率切片

#### 1.1 数据结构与迁移定义 (V020)
- **迁移脚本**：`sql/migrations/{sqlite,postgres}/V020__ledger_snapshots_and_tx_fx.sql`
- **快照表定义**：
  ```sql
  CREATE TABLE IF NOT EXISTS ledger_snapshots (
      id                  INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id             INTEGER NOT NULL,
      asset_id            INTEGER NOT NULL,
      snapshot_date       TEXT NOT NULL,        -- 'YYYY-MM-DD'
      quantity            TEXT NOT NULL,        -- 128-bit decimal string
      cost_basis          TEXT NOT NULL,        -- 128-bit decimal string
      net_value           TEXT NOT NULL,        -- 128-bit decimal string
      current_value       TEXT NOT NULL,        -- 128-bit decimal string
      realized_pnl        TEXT NOT NULL,        -- 128-bit decimal string
      balance             TEXT NOT NULL,        -- 现金/负债余额
      tx_checkpoint_date  TEXT NOT NULL,        -- 包含的最大交易日期
      tx_checkpoint_id    INTEGER NOT NULL,     -- 包含的最大交易 ID
      lots_snapshot_json  TEXT,                 -- 用于 Milestone 2 FIFO 未结批次向下兼容序列化
      created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
      UNIQUE(user_id, asset_id, snapshot_date)
  );
  CREATE INDEX IF NOT EXISTS idx_snapshots_lookup ON ledger_snapshots(user_id, asset_id, snapshot_date DESC);

  -- 为 transactions 增加历史生效汇率字段
  ALTER TABLE transactions ADD COLUMN fx_rate_to_cny REAL DEFAULT 1.0;
  ```
- **存量数据平滑回填**：
  - 本币交易（CNY）统一置 `fx_rate_to_cny = 1.0`；
  - 外币交易（USD, HKD, EUR 等）通过迁移脚本，按 `transactions.transaction_date` 关联匹配 `exchange_rate_history` 历史汇率落库，无历史记录者平滑回退至当前汇率。

#### 1.2 快照生成与增量回放逻辑 (`core/ledger/ledger_engine.c`)
- **增量重放核心**：
  1. 重算特定资产时，优先查询 `SELECT * FROM ledger_snapshots WHERE user_id=? AND asset_id=? AND snapshot_date <= target_date ORDER BY snapshot_date DESC LIMIT 1`；
  2. 若找到快照，以快照数据初始化 `curr_qty`, `curr_cost`, `curr_net`, `balance`；
  3. 后续重放严格按 `ORDER BY transaction_date ASC, id ASC`，仅查询 `transaction_date > tx_checkpoint_date OR (transaction_date = tx_checkpoint_date AND id > tx_checkpoint_id)` 的后续增量交易（复杂度 $O(\Delta)$）；
  4. 若未找到快照，保持原有的全量从零重放。
- **快照写入规则**：
  - 提供 `ledger_snapshot_create(pool, user_id, asset_id, date)`，在交易记录回放达到月末或增量交易数超阈值时物化落库。

#### 1.3 历史补记与交易变更的快照级联失效机制
- 在 `application/transaction/usecases.c` 的 `tx_usecase_create`、`tx_usecase_update`、`tx_usecase_delete` 中：
  - 当变动的交易发生时间为 `tx_date` 时，在同一事务作用域 `db_tx_scope_t` 内执行：
    `DELETE FROM ledger_snapshots WHERE user_id=? AND asset_id=? AND snapshot_date >= ?;`
  - 失效后触发就近前驱快照的增量重算并回填，杜绝历史穿插记账导致的数据漂移。

#### 1.4 历史汇率切片应用 (`portfolio_repo_impl.c`)
- 重构 `reports/asset/trend` 和 `reports/multi-currency-summary`：
  - 弃用全量按当前汇率折算的 `exchange_rate_get_to_cny(cur)`；
  - 改用按历史时点抽样的汇率切片函数 `exchange_rate_get_historical_or_latest(pool, cur, sample_date)`，使历史资产走势图反映当时真实的汇率波动。

#### 1.5 细分实施子任务（Milestone 1 Tasks）
- [x] Task 1.1: 编写 SQLite 与 PostgreSQL 的 V020 迁移脚本与汇率回填逻辑。
- [x] Task 1.2: 在 `core/ledger/` 中实现快照查询、反序列化与增量重放。
- [x] Task 1.3: 在交易增删改用例中实现快照级联失效与联动重算。
- [x] Task 1.4: 在投资组合报表中应用历史时点汇率查询。
- [x] Task 1.5: 编写 `test_ledger_rebuild_checkpoint.c` 并在 `test_link.sh` 中验证通过。

---

### 里程碑 2：多策略成本结转与先进先出（FIFO）批次核算

#### 2.1 资产成本策略元数据扩展 (V021)
- **迁移脚本**：`sql/migrations/{sqlite,postgres}/V021__asset_cost_method_and_lots.sql`
- **Schema 定义**：
  ```sql
  ALTER TABLE assets ADD COLUMN cost_method TEXT DEFAULT 'weighted_average'; -- 'weighted_average', 'fifo'

  CREATE TABLE IF NOT EXISTS transaction_lots (
      id                  INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id             INTEGER NOT NULL,
      asset_id            INTEGER NOT NULL,
      buy_tx_id           INTEGER NOT NULL REFERENCES transactions(id),
      buy_date            TEXT NOT NULL,
      original_quantity   TEXT NOT NULL,
      remaining_quantity  TEXT NOT NULL,
      cost_per_unit       TEXT NOT NULL,
      fee_allocated       TEXT NOT NULL,
      status              TEXT DEFAULT 'open', -- 'open', 'closed'
      created_at          TEXT DEFAULT CURRENT_TIMESTAMP
  );
  CREATE INDEX IF NOT EXISTS idx_lots_open ON transaction_lots(user_id, asset_id, status, buy_date ASC, id ASC);
  ```

#### 2.2 策略契约接口与算法实现 (`core/ledger/ledger_engine.c`)
- 定义成本策略算子：
  ```c
  typedef struct {
      int (*calc_buy)(quantity_t prev_qty, money_t prev_cost, quantity_t buy_qty, price_t buy_price, money_t buy_fee, quantity_t* out_qty, money_t* out_cost);
      int (*calc_sell)(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, quantity_t sell_qty, price_t sell_price, money_t sell_fee, quantity_t* out_qty, money_t* out_cost, money_t* out_cost_reduction, money_t* out_realized_pnl);
  } ledger_cost_strategy_t;
  ```
- **移动加权平均实现**：维持原算子无缝对接，不读写 `transaction_lots` 表。
- **FIFO 批次实现**：
  - **买入**：在 `transaction_lots` 中新增一条 `remaining_quantity = buy_qty`，`cost_per_unit = (buy_gross + buy_fee) / buy_qty` 的批次记录。
  - **卖出**：按 `buy_date ASC, id ASC` 锁定 `status='open'` 的批次队列，逐个扣除可用份额，直至满足本次 `sell_qty`；针对每个消耗的批次分别计算其本金扣减，汇总得到绝对精确的 `realized_pnl`。
  - **快照保存**：在为 FIFO 资产生成快照时，将当前所有 open 批次的简要状态序列化存入 `ledger_snapshots.lots_snapshot_json`。

#### 2.3 资产创建与更新规则保护 (`application/asset/usecases.c`)
- `asset_usecase_create` 接收可选的 `cost_method` 参数；
- `asset_usecase_update` 校验：若资产已存在相关交易，拒绝修改 `cost_method` 并返回 HTTP 1004。

#### 2.4 细分实施子任务（Milestone 2 Tasks）
- [x] Task 2.1: 编写 V021 批次表与资产字段迁移脚本。
- [x] Task 2.2: 在 `core/ledger/` 中抽象并实现成本策略接口（加权平均 + FIFO）。
- [x] Task 2.3: 实现 FIFO 卖出批次逐层核销与盈亏计算，快照支持批次序列化。
- [x] Task 2.4: 在资产用例中增加成本法锁定校验。
- [x] Task 2.5: 编写 `test_cost_basis_fifo.c` 验证各种买入/卖出交叉算例与盈亏精确度。

---

### 里程碑 3：AI 上下文投影精简与复合批量操作协议

#### 3.1 工具返回值模型投影层 (`dispatcher.c` & `fast_json.c`)
- **投影规则**：只读工具在将查询结果传给大模型前，过滤掉技术性冗余字段：
  - `get_assets`：保留 `id`, `name`, `asset_type`, `currency`, `balance`, `current_value`, `cost_basis`，剥离 `user_id`, `created_at`, `updated_at`, `note`, `icon`, `sort_order` 等；
  - `get_daily_expenses` / `get_transactions`：限制单次最多回传 15 条紧凑数组，仅保留日期、金额、分类名、资产名与备注；
- **成果预期**：输入上下文 Token 降低 70% 以上，彻底杜绝多轮工具调用引发的 4096+ Token 暴涨。

#### 3.2 复合批量动作与防重放签名协议 (`expense_tool.c` / `confirmation.c`)
- **工具定义**：`propose_batch_actions`，输入为动作数组：
  ```json
  {
    "actions": [
      {"action_type": "daily_expense", "amount": 45.0, "type": "expense", "category_id": 3, "asset_id": 1, "note": "午餐"},
      {"action_type": "transfer", "amount": 1000.0, "from_asset_id": 1, "to_asset_id": 2, "note": "还信用卡"}
    ]
  }
  ```
- **批量 HMAC 签名**：
  - 计算所有子操作规范 JSON 的 Hash 合集；
  - 生成 `mf_batch_v1.<payload>.<sig>` 签名令牌，存入 Nonce 校验栈；
- **原子执行端点**：`POST /api/ai/actions/confirm-batch`：
  - 验证 Nonce 与 HMAC；
  - 启动外层 `db_tx_scope_t` 保存点；
  - 逐项调用对应的 application usecase；
  - 若任一项出错，整批回滚并返回具体失败原因；全部成功则单事务提交。

#### 3.3 细分实施子任务（Milestone 3 Tasks）
- [x] Task 3.1: 在 `dispatcher.c` 中实现针对模型的返回值紧凑投影过滤函数。
- [x] Task 3.2: 在 `confirmation.h/.c` 中扩展支持批量操作的 Hash 计算与验签。
- [x] Task 3.3: 注册 `propose_batch_actions` 工具并编写对应参数校验。
- [x] Task 3.4: 实现 `POST /api/ai/actions/confirm-batch` 原子执行端点。
- [x] Task 3.5: 编写 `test_ai_token_compact.c` 与 `test_ai_batch_actions.sh` 集成测试。

---

### 里程碑 4：动态条件分支与 Saga 补偿型工作流引擎

#### 4.1 节点扩展与分支状态机 (`workflow/engine.h` & `executor.c`)
- **扩展节点定义**：
  ```c
  typedef bool (*ai_wf_condition_fn)(const csilk_json_t* shared_state);
  typedef int  (*ai_wf_compensate_fn)(csilk_db_pool_t* pool, int64_t user_id, csilk_json_t* shared_state);

  struct ai_workflow_node {
      const char*          name;
      const char*          description;
      ai_wf_step_fn        execute;
      ai_wf_condition_fn   condition;     /**< 前置判定条件：返回 false 则跳过该步骤 */
      ai_wf_compensate_fn  compensate;    /**< 失败补偿清理函数 */
      int                  max_retries;   /**< 瞬时重试次数 */
      const char*          next_step;     /**< 默认下一步 */
  };
  ```
- **执行器流程**：
  1. 顺序遍历执行节点前，先求值 `condition`，为 false 则流式抛出 `step_skipped` 事件；
  2. 若执行失败且 `retries < max_retries`，进行毫秒级退避重试；
  3. 若重试耗尽且不可恢复，中止工作流，并逆序触发此前已成功节点的 `compensate` 回调（如清理临时生成的未确认建议），最后发送 `workflow_failed` 事件。

#### 4.2 现有 13 个工作流的平滑升级
- 保持现有内置工作流默认 `condition = NULL`, `compensate = NULL`，确保向后兼容；
- 为关键分析流（如 `financial_health`、`portfolio_analysis`）增加条件跳步与降级数据回退逻辑。

#### 4.3 细分实施子任务（Milestone 4 Tasks）
- [x] Task 4.1: 在 `workflow/engine.h` 中扩展节点数据结构。
- [x] Task 4.2: 在 `executor.c` 中增加条件判定、步骤重试与逆向 Saga 补偿栈逻辑。
- [x] Task 4.3: 为 SSE 新增 `step_skipped` 与 `step_compensated` 事件类型。
- [x] Task 4.4: 升级 1-2 个代表性工作流以验证动态分支与错误回退。
- [x] Task 4.5: 编写 `test_workflow_branching.c` 与集成测试套件。

---

## 5. 阶段递进验证矩阵 (Verification & Regression Matrix)

| 里程碑 | 目标文件 | 核心测试手段 | 准出指标 |
| :--- | :--- | :--- | :--- |
| **M1** | `ledger_engine.c`, `portfolio_repo_impl.c`, `V020*.sql` | `test_ledger_rebuild_checkpoint.c`, `test_link.sh` | CTest 46/46 全绿，历史重放 $O(\Delta)$ 耗时降低 80%+ |
| **M2** | `ledger_engine.c`, `asset_repo_impl.c`, `V021*.sql` | `test_cost_basis_fifo.c`, `test_link.sh` | FIFO 逐笔卖出盈亏与标准算例一致，加权平均资产零行为变化 |
| **M3** | `dispatcher.c`, `expense_tool.c`, `confirmation.c` | `test_ai_token_compact.c`, `test_ai_loop_hardening.sh` | Token 消耗降低 70%+，批量入账原子性 100% |
| **M4** | `workflow/engine.c`, `workflow/executor.c` | `test_workflow_branching.c`, `test_link.sh` | 分支命中正确，失败场景 100% 触发补偿清理 |

---

## 6. 留给构建者的工程决策 (What's Left to the Builder)

1. **快照自适应触发阈值**：默认在月末生成快照；构建者可根据单资产在当月的交易增量（如超过 50 笔）自适应追加阶段快照。
2. **字段投影的微调**：构建者可根据实际的大模型提示词质量，动态增删 `dispatcher.c` 中各工具的紧凑字段白名单。
3. **重试退避间隔**：工作流瞬时步骤失败重试的退避策略（推荐指数退避 100ms/200ms）。
