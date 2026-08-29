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

### Phase 3: wf_anomaly_detect (next)

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
