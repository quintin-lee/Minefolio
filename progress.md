# Progress Log

## Session: 2026-08-29 — 7 New Workflows Implementation

### Phase 0: 基建与规划
- **Status:** complete (2026-08-29)
- Actions: 调研 ai_workflow_service.c + 数据模型，确定优先级 P0>P1>P2，制定 task_plan.md/findings.md

### Phase 1: wf_payday_split
- **Status:** complete (2026-08-29)
- Commits: 46332ffe (wf_payday_split 3 steps + g_workflow registration)
- Steps: step_payday_detect / step_payday_allocate / step_payday_report — 校验通过，cmake build ✅
- Notes: block comments + SQL intent markers (HINT:...) are required markers for follow-up lint, kept intentionally

### Phase 2: wf_budget_guard
- **Status:** complete (2026-08-29)
- Steps: step_bg_collect / step_bg_forecast / step_bg_report — cmake build ✅
- Notes: implicit budget = HIST_AVG*1.2, fallback to projected when no history; risk thresholds 80%/100%

### Phase 3: wf_anomaly_detect
- **Status:** complete (2026-08-29)
- Steps: step_ad_collect / step_ad_score / step_ad_report — 4-rule engine + Mermaid pie, cmake build ✅
- Notes: 3σ with fallback (sparse→fixed threshold 2000), duplicate within 2 days, midnight 00-05 >500, freq small <50 x5/day
- Commit: pending

### Phase 4: wf_subscription_audit
- **Status:** complete (2026-08-29)
- Commit: pending (wf_subscription_audit 3 steps + g_workflow registration)
- Steps: step_sa_collect / step_sa_analyze / step_sa_report — (amount+category)聚类 cnt>=3 跨月>=2, stale>45d, hiked +10%, cmake build ✅

### Phase 5: wf_emergency_fund
- **Status:** complete (2026-08-29)
- Steps: step_ef_collect / step_ef_health / step_ef_report — health_score 0-100 + gap + topup, cmake build ✅
- Commit: pending

### Phase 6: wf_goal_tracker
- **Status:** complete (2026-08-29)
- Steps: step_gt_collect / step_gt_plan / step_gt_report — 懒创建 savings_goals + 月需追加 + 饼图/xychart, cmake build ✅
- Commit: pending

### Phase 7: wf_debt_payoff
- **Status:** complete (2026-08-29)
- Steps: step_dp_collect / step_dp_simulate / step_dp_report — note解析利率 + 12期雪崩/雪球模拟 + 饼图+对比, cmake build ✅
- Commit: pending

### Phase 8: 收尾
- **Status:** complete (2026-08-29)
- 全量 10 工作流：cmake ✅ + npm run build ✅ (vite 5.4.21, vue-tsc strict)
- Commits: 0f43a0f8 (debt_payoff) 完成 7/7 新工作流闭环

### Phase 9: 前端工作流入口
- **Status:** complete (2026-08-29)
- 文件：`frontend/src/components/WorkflowBar.vue` (+128/-5)
- 变更：参数对话框覆盖 7 新工作流（payday 4比例 / budget月份 / anomaly&subscription回溯 / emergency目标月数 / debt月供）+ DIRECT_RUN_IDS 直启 + ratioSum 归一化提示 + 新样式
- 构建：`npm run build` ✅ (vite 5.4.21, 1m04s)

### Phase 10: wf_cashflow_forecast
- **Status:** complete (2026-08-30)
- Commit: a214bcb6 (wf_cashflow_forecast 3 steps + g_workflow)
- Steps: step_cf_collect / step_cf_forecast / step_cf_report — 6月滚动现金预测 + xychart-beta, cmake build ✅

### Phase 11: wf_bill_calendar
- **Status:** complete (2026-08-30)
- Steps: step_bc_collect / step_bc_calendar / step_bc_report — 债务+订阅账单汇聚 + 30日hash日历 + xychart压力分布 + 高压日检测, cmake build ✅
- Commit: dab873a4

### Phase 12: wf_health_score
- **Status:** complete (2026-08-30)
- Steps: step_hs_collect / step_hs_score / step_hs_report — 4维度加权(流动/负债/储蓄/纪律各25) → 0-100 + grade, xychart-bar + 短板建议, cmake build ✅
- Commit: pending

## Session: 2025-08-21

### Phase 1: 完整代码分析
- **Status:** complete
- **Started:** 2025-08-21
- Actions: 全面阅读后端 54 个源文件 + 前端 60+ 文件，输出完整架构分析报告
- Files: task_plan.md, findings.md, progress.md（新建）

### Phase 2: 高危 Bug 修复
- **Status:** complete
- **Commit:** f2791d2d
- 修复内容：
  1. `transactions_update()` 投资类→非投资类切换时旧持仓余额未回滚（核心 bug）
  2. `auth_service.c` 缺少 `#include <stdio.h>` 编译失败
  3. 密码长度校验统一为 ≥6
- 验证：`cmake --build` ✅ / `npm run build` ✅
- `test_link.sh` 因 csilk 框架空响应问题无法运行（预存问题，与本次修改无关）

### Phase 3: 代码可读性重构
- **Status:** complete
- **Commit:** f2791d2d
- 修复内容：`transactions_update`/`transactions_delete` 中 `atoll(id_str)` 重复调用 → 提取 `tx_id_val` 局部变量

### Phase 4: 测试覆盖补齐
- **Status:** complete
- **Commit:** 4c51b834
- 新增测试用例：
  - **J1**: 交易更新 buy→deposit，验证 quantity/cost_basis/net_value 回零 + 钱包余额还原
  - **J2**: 无 note 的 buy+fee 交易，验证 fee 行 note="fee"（非空 fallback）

### Phase 5: 前端清理
- **Status:** complete
- 检查结论：所有 TypeScript 类型均有引用，移动端无残留，offline-http.ts 正常

### Phase 6: 文档与配置
- **Status:** complete
- **Commit:** f2791d2d
- AGENTS.md 新增 Gotcha #8（密码长度）和 #9（投资类回滚规则）和 #10（JWT Dockerfile 说明）

## Test Results
| Check | Result |
|-------|--------|
| 后端编译 | ✅ cmake --build 通过 |
| 前端构建 | ✅ npm run build 通过 |
| test_link.sh | ⚠️ csilk 框架空响应（Content-Length: 0），预存问题，与本次修改无关 |
| 新测试 J1/J2 | 已写入 test_link.sh，待框架问题修复后执行 |

## Error Log
| Timestamp | Error | Attempt | Resolution |
|-----------|-------|---------|------------|
| 2025-08-21 | auth_service.c:334 snprintf 隐式声明 | 1 | 添加 #include <stdio.h> |
| 2025-08-21 | test_link.sh Content-Length:0 空响应 | 1 | 确认为 csilk 框架预存问题，已记录 |
