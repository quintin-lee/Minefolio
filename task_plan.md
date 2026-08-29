# Task Plan: 7 New Practical Workflows (Minefolio)

## Goal
逐个实现 7 个新增实用工作流，补齐“月度复盘/再平衡/大额决策”之外的核心日常财务场景。每个工作流需：后端 SSE 步骤定义 + 真实数据聚合 + Mermaid 报告 + 前端可视化复用现有 ai_workflow_service 框架。

## Current Phase
Phase 2 — 预算超支预警 (Budget Guard) [pending — next]

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
- [ ] 步骤：`de_monthly_by_category` + 预算表/或基于历史均值推算预算
- [ ] 预测：按日进度外推月底总额，计算超支风险分类
- [ ] 报告：进度条 + Mermaid 柱状图 + 3 条节流建议
- [ ] 注册 `wf_budget_guard`
- **Status:** pending

### Phase 3: 异常交易检测 (Anomaly Detect) ⭐ P0
- [ ] 规则引擎：金额 3σ / 重复扣款 / 凌晨大额 / 高频小额
- [ ] AI 二次判断占位（当前用规则分数）
- [ ] 报告：异常清单表格 + 处理建议
- [ ] 注册 `wf_anomaly_detect`
- **Status:** pending

### Phase 4: 订阅/固定支出审计 (Subscription Audit) — P1
- [ ] 聚类：按 note/amount/周期识别订阅项
- [ ] 涨价检测 + 90天未使用标记
- [ ] 报告：订阅清单 + 年化节省额
- **Status:** pending

### Phase 5: 应急基金健康检查 (Emergency Fund Check) — P1
- [ ] 复用 `get_user_avg_monthly_burn` + liquid_cash
- [ ] 健康度评分 0-100 + 缺口计算
- [ ] 报告：仪表盘 + 补足计划
- **Status:** pending

### Phase 6: 目标储蓄追踪 (Goal Tracker) — P2
- [ ] 需新增 `savings_goals` 表或复用资产 tag 方案（待定）
- [ ] 进度/剩余月数/需追加额计算
- [ ] 报告：进度条 + 达成预测
- **Status:** pending

### Phase 7: 债务加速偿还规划 (Debt Payoff) — P2
- [ ] 拉取 loan/credit_card 余额+利率
- [ ] 雪崩 vs 雪球模拟 12 期
- [ ] 报告：对比表 + 利息节省
- **Status:** pending

### Phase 8: 收尾
- [ ] 全量 10 工作流回归构建 (`cmake --build && npm run build`)
- [ ] 清理 build 产物、更新 AGENTS.md/文档
- **Status:** pending

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
