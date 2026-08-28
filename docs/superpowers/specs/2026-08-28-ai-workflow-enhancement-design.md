# AI 财务工作流增强系统设计规范 (AI Financial Workflow Design)

## 1. 概述与目标

在 Minefolio 现有 AI 问答与单轮工具调用的基础上，构建**多步骤、场景化的财务 AI 工作流（Financial Workflow Engine）**。
通过预置的流水线化分析（如月末财务复盘、投资再平衡体检、大额支出智能决策），将原本散碎的多轮提问升级为**一键式深度数据汇总、多维交叉建模、诊断打分与结构化报告生成**，并在前端 Chat 界面中通过实时 SSE 步骤进度卡片进行可视化呈现。

---

## 2. 核心预置财务工作流定义

| 工作流 ID | 名称 | 图标 | 业务步骤流水线 |
| :--- | :--- | :--- | :--- |
| `wf_monthly_review` | **月末财务深度复盘** | `ph:calendar-check` | **① 数据汇总**（收支分类与交易流向） $\to$ **② 趋势与异动分析**（环比涨跌与超支项） $\to$ **③ 财务健康度打分**（储蓄率/负债率） $\to$ **④ 生成复盘报告与流向图** |
| `wf_portfolio_rebalance` | **投资组合再平衡体检** | `ph:chart-polar` | **① 全局持仓扫描**（股票/基金/债券/现金市值） $\to$ **② 大类资产敞口测算**（偏离度分析） $\to$ **③ 盈亏归因分析** $\to$ **④ 调仓方案与交易草案生成** |
| `wf_expense_decision` | **大额支出决策评估** | `ph:scales` | **① 流动性与备用金核算** $\to$ **② 现金流压力测试**（3~6个月安全边际） $\to$ **③ 支付方式测算**（全款 vs 分期成本） $\to$ **④ 决策建议与支出备忘草案** |

---

## 3. 系统架构与协议规范

### 3.1 后端 C 语言工作流引擎 (`ai_workflow_service.c`)
- **定义工作流元数据与步骤执行器**：
  - 每个 Step 具备独立的执行逻辑（如直接调用仓储层获取聚合指标，或通过精炼 Prompt 请求模型执行特定维度的推理分析）；
  - 上一步的输出作为结构化上下文自动注入下一步。
- **SSE 事件协议扩展**：
  - `event: workflow_start`：推送工作流基本信息与总步数；
  - `event: step_start`：通知前端当前正在执行的步骤（`step_id`, `title`）；
  - `event: step_progress`：推送步骤执行中的实时中间状态（如“已分析 15 类日常收支...”）；
  - `event: step_complete`：步骤成功完成，携带步骤输出摘要（`summary`）；
  - `event: token`：最终综合诊断报告的流式输出；
  - `event: workflow_complete`：全流程执行完毕并落库 `ai_messages`。

### 3.2 前端 Chat 交互层
1. **工作流快捷面板 (`WorkflowBar.vue`)**：
   - 位于 Chat 消息列表与输入框之间，展示预置工作流卡片；
   - 点击可唤起轻量参数弹窗（如指定月份、指定金额）或一键极速触发。
2. **步骤进度卡片 (`WorkflowProgressCard.vue`)**：
   - 集成在消息气泡中，以深色时间轴展示各 Step 的执行状态（等待中、运行中脉冲、已完成对勾、失败红叉）；
   - 支持展开/折叠各步骤的中间数据摘要。
3. **结构化报告渲染**：
   - 自动继承已实现的 `CodeBlock` 高亮、`MermaidBlock` 图表展示与 `ActionCard` 操作草案确认能力。

---

## 4. 接口与数据契约

1. `GET /api/ai/workflows`
   - **返回**：`{ code: 0, data: [ { id, title, description, icon, steps: [...] } ] }`
2. `POST /api/ai/workflows/run`
   - **入参**：`{ workflow_id: string, session_id?: number, params?: Record<string, any> }`
   - **返回**：`text/event-stream`（遵循上述 SSE 事件流契约）

---

## 5. 验证计划

1. **后端验证**：
   - C 语言编译无警告，`tests/test_link.sh` 全量通过；
   - 针对 `/api/ai/workflows` 与 `/api/ai/workflows/run` 编写端到端自动化测试用例，验证 Step 状态机与 SSE 流式推送。
2. **前端验证**：
   - `npm --prefix frontend run build`（0 错误）与 `npm --prefix frontend test`（测试 100% 通过）；
   - 在 Chat 界面中触发各工作流，验证步骤卡片动态更新、数据汇总准确性与最终 Mermaid/Markdown 报告渲染。
