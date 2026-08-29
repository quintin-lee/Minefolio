# Task Plan: 7 New Practical Workflows (Minefolio)

## Goal
逐个实现 7 个新增实用工作流，补齐“月度复盘/再平衡/大额决策”之外的核心日常财务场景。每个工作流需：后端 SSE 步骤定义 + 真实数据聚合 + Mermaid 报告 + 前端可视化复用现有 ai_workflow_service 框架。

## Current Phase
Phase 7 — 债务加速偿还规划 (Debt Payoff) [in_progress]

## Phases

### Phase 0: 基建与规划（已完成）
- [x] 调研现有 3 工作流架构（ai_workflow_service.c: 1212行，helpers/get_current_* + step_* + g_workflows 注册表 + ai_workflow_run_handler SSE）
- [x] 评估用户画像与数据表：assets, transactions, daily_expenses, categories, dca 等
- [x] 确定 7 工作流优先级：P0(1,3,4) > P1(2,5) > P2(6,7)
- [x] 制定本计划文件与 findings/progress 结构
- **Status:** complete

### Phase 1: 工资到账自动分配 (Payday Auto-Split) ⭐ P0
- [x] 后端：`step_payday_detect` — 识别本月 income 交易/日常收入，计算可分配总额
- [x] 后端：`step_payday_allocate` — 按 50/20/20/10 或自定义比例计算分配方案，对比目标/预算
- [x] 后端：`step_payday_report` — 生成分配清单 + Mermaid 饼图 + 待确认转账草案 (action JSON)
- [x] 注册 `wf_payday_split` 到 g_workflows
- [x] 前端：验证 workflow 定义可被 `ai_workflow_get_definitions_json` 暴露（无需前端改动，自动暴露）
- [x] 构建 + 手动 SSE 测试（cmake build passed）
- **Status:** complete

### Phase 2: 预算超支预警 (Budget Guard) ⭐ P0
- [x] 步骤：`step_bg_collect` — de_monthly_by_category + 历史均值×1.2 隐式预算
- [x] 预测：`step_bg_forecast` — 按日进度外推月底总额，风险分级 danger/warning/safe
- [x] 报告：`step_bg_report` — 表格 + Mermaid xychart + 3 条节流建议
- [x] 注册 `wf_budget_guard` 到 g_workflows
- [x] 构建验证通过
- **Status:** complete

### Phase 3: 异常交易检测 (Anomaly Detect) ⭐ P0
- [x] 规则引擎：金额 3σ / 重复扣款 / 凌晨大额 / 高频小额 — `step_ad_collect` + `step_ad_score`
- [x] AI 二次判断占位（当前用规则分数 60-85）
- [x] 报告：异常清单表格 + Mermaid 饼图 + 处理建议 — `step_ad_report`
- [x] 注册 `wf_anomaly_detect` 到 g_workflows（ph:shield-warning）
- [x] 构建验证通过（cmake build passed）
- **Status:** complete

### Phase 4: 订阅/固定支出审计 (Subscription Audit) — P1
- [x] 聚类：按 (amount+category) 聚类，cnt>=3 且跨月>=2 识别订阅 — `step_sa_collect` + `step_sa_analyze`
- [x] 涨价检测（同类目替代金额+10%） + 45天未使用标记 stale/hiked
- [x] 报告：订阅清单表格 + Mermaid 饼图 + 年化额 + 批处理建议 — `step_sa_report`
- [x] 注册 `wf_subscription_audit` 到 g_workflows（ph:repeat, sa_collect/sa_analyze/generate_report）
- [x] 构建验证通过（cmake build passed, 仅 strncpy 截断 warning）
- **Status:** complete

### Phase 5: 应急基金健康检查 (Emergency Fund Check) — P1
- [x] 复用 `get_user_avg_monthly_burn` + liquid_cash — `step_ef_collect`
- [x] 健康度评分 0-100 + 缺口/覆盖月数/补足计划 — `step_ef_health`
- [x] 报告：达成度饼图 + 补足计划表 + 分级建议 — `step_ef_report`
- [x] 注册 `wf_emergency_fund` 到 g_workflows（ph:shield-check, ef_collect/ef_health/generate_report）
- [x] 构建验证通过（cmake build passed, 仅 strncpy warning）
- **Status:** complete

### Phase 6: 目标储蓄追踪 (Goal Tracker) — P2
- [x] 懒创建 `savings_goals` 表 + 全量查询 `step_gt_collect`
- [x] 进度/剩余月数/月需追加测算 `step_gt_plan`
- [x] 报告：总进度饼图 + xychart 进度条 + 明细表 + action `step_gt_report`
- [x] 注册 `wf_goal_tracker` 到 g_workflows（ph:target, gt_collect/gt_plan/generate_report）
- [x] 构建验证通过（仅预存 strncpy warning）
- **Status:** complete

### Phase 7: 债务加速偿还规划 (Debt Payoff) — P2
- [x] 拉取 loan/credit_card 余额+利率（note 解析 + 默认 18%/4.9%/6%）— `step_dp_collect`
- [x] 雪崩 vs 雪球模拟 12 期（minPay max(100,2%)+优先级定向）— `step_dp_simulate`
- [x] 报告：债务饼图 + xychart 利息对比 + 明细表 + 推荐策略 — `step_dp_report`
- [x] 注册 `wf_debt_payoff` 到 g_workflows（ph:hand-coins, dp_collect/dp_simulate/generate_report）
- [x] 构建验证通过（仅预存 strncpy warning）
- **Status:** complete

### Phase 8: 收尾
- [x] 全量 10 工作流回归构建（backend cmake ✅，仅 strncpy warning）
- [ ] 前端 npm run build 回归 + 清理 build 产物
- **Status:** in_progress

## Key Questions
1. 预算数据来源：当前无独立 budgets 表，Phase2 需用历史均值*1.2 作为隐式预算，还是新增表？
2. savings_goals 是否新增表或先用 asset 备注关联？倾向新增表以支持长期追踪
3. 订阅聚类精度：首版用 (amount + note 归一化) 聚类是否足够？
4. 异常检测阈值：3σ 在样本少时不稳定，需 fallback 固定阈值

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| P0 优先实现 1/3/4 | 高频、强感知、仅依赖现有表 |
| 复用 ai_workflow_service 框架 | 保持 SSE/ctx 传递一致性，避免新服务 |
| 每个工作流 3 步 | 与现有 portfolio/expense 保持一致，降低前端改动 |
| Mermaid 图表必选 | 前端已支持 Mermaid 渲染，复用 |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| — | — | — |

## Notes
- 每次新增工作流后增量构建验证
- 保持中文报告风格与现有 3 工作流一致
