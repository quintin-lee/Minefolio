# AI 助手全方位升级实施计划 (AI Assistant Comprehensive Upgrade Implementation Plan)

- **创建日期**：2026-08-27
- **关联规范**：`docs/superpowers/specs/2026-08-27-ai-assistant-comprehensive-upgrade-design.md`

## 实施阶段与任务列表

### 阶段 1：后端 AI Tools 扩展 (Backend)
- [ ] 在 `backend/src/services/ai_tools.c` 中定义 `schema_propose_daily_expense`、`schema_propose_transfer`、`schema_analyze_financial_health`；
- [ ] 实现 `exec_propose_daily_expense`：模糊匹配用户分类与资产，生成规范收支拟录入草稿；
- [ ] 实现 `exec_propose_transfer`：模糊匹配转出与转入资产，生成规范转账拟录入草稿；
- [ ] 实现 `exec_analyze_financial_health`：聚合流动性月数、净储蓄率、资产负债率、生息投资资产占比；
- [ ] 在 `s_tools` 中注册并在 `ai_tools_execute` 中分发（工具总数扩充至 15 个）；
- [ ] 编译后端并通过单元测试验证各工具输出。

### 阶段 2：前端交互组件开发 (Frontend)
- [ ] 创建 `frontend/src/components/ActionCard.vue`：支持金额/分类/账户微调、两步确认入库、联动刷新；
- [ ] 创建 `frontend/src/components/PromptStarters.vue`：提供 5 大常用场景快捷引导气泡；
- [ ] 增强 `frontend/src/views/ChatView.vue`（桌面端）：嵌入 ActionCard 解析渲染、PromptStarters、导出 Markdown 与复制功能；
- [ ] 增强 `frontend/src/views-mobile/ChatView.vue`（移动端）：适配移动端 ActionCard 与快捷引导；
- [ ] 执行 `npm --prefix frontend run build` 确保 TypeScript 严格类型检查 100% 通过。

### 阶段 3：全量验证与代码推送 (Verification & Push)
- [ ] 运行 `./tests/test_link.sh` 确保 114 个后端集成测试用例全部通过；
- [ ] 重启后端服务并验证健康检查；
- [ ] Git 提交并推送代码到 GitHub 仓库 (`git push origin master`)。
