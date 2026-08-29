# Findings — 7 New Workflows

## Existing Workflow Architecture
- 文件：`backend/src/services/ai_workflow_service.c` (1212 行)
- 3 helpers: `get_current_month_str`, `get_current_datetime_str`, `get_prev_month_str` (已实时化), `get_user_avg_monthly_burn`
- 3 workflows in `g_workflows[]`:
  - wf_monthly_review (4 steps)
  - wf_portfolio_rebalance (3 steps)
  - wf_expense_decision (3 steps)
- 执行器：`ai_workflow_run_handler` — SSE 流式，ctx_obj 累积各步 JSON，末步走 `ai_service_stream_report` 或 fallback
- 数据源：`asset_list`, `tx_monthly`, `de_monthly_totals`, `de_monthly_by_category`, `ai_session_insert` 等

## Data Model Inventory
- `assets`: id,user_id,name,asset_type,cash/bank/stock/fund/crypto/bond/loan/credit_card, balance/current_value, cost_basis, quantity
- `transactions`: id,user_id,asset_id,linked_asset_id,amount,transaction_type, direction, linked_direction, parent_tx_id, note, created_at
- `daily_expenses`: id,user_id,amount,expense_type(income/expense),category_id,expense_date,asset_id,note
- `categories`: 用户分类树
- `dca` / `ai_sessions` 等
- 无独立 budgets / savings_goals 表 — Phase2/6 需决策

## Reusable Helpers
- `asset_list(pool,uid,1,100,NULL,&total)` — 拉全量资产
- `de_monthly_totals`, `de_monthly_by_category` — 月度收支
- `tx_monthly` — 交易月度汇总
- `get_user_avg_monthly_burn` — 历史月均刚性支出
- `balance_apply_delta` — 不在 workflow 用，仅交易链路

## Design Decisions for New Workflows
### WF1 Payday Split
- 可分配总额 = 本月 income 型 daily_expenses + transactions inflows (deposit/income/transfer_in)
- 分配基准：默认 50%生活/20%投资/20%还贷/10%应急，可从 params 覆盖
- 步骤：detect → allocate → report

### WF2 Budget Guard
- 预算来源：若无 budgets 表，用近3月同分类均值 * 1.1 作为预算线
- 预测：日均 * 当月天数，进度 = 已花/预算

### WF3 Anomaly
- 规则：单笔 > 均值+3σ 且 >5000；24h 内同商户(按 note 前缀)同金额重复；0-5 点大额>1000；单日>10 笔小额<50

## Frontend Contract
- `ai_workflow_get_definitions_json` 自动暴露新 workflow，无需前端改动即可在工作流列表显示
- 报告为 Markdown + Mermaid，前端已支持渲染
- action JSON 块 ` ```action {action_type:...}` 可被前端解析为可执行卡片

## Risks
- 无 budgets 表时 Budget Guard 的“预算”需明确为“估算预算”避免误导
- 订阅审计的聚类在首版可能召回率低，需标注“实验性”
