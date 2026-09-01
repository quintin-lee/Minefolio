# 智能财务工作流交互优化设计方案 (Design Specification)

## 1. 概述与设计背景

Minefolio 具备完善的 C23 + csilk 后端与 Vue 3 智能财务工作流能力（支持收支复盘、预算守护、投资再平衡、发薪日分流等 13 个多步流水线分析）。目前原版前端交互存在常驻 `WorkflowBar` 纵向占屏大、居中模态弹窗割裂对话流、中间步骤无法检视、以及分析结论缺少业务闭环等痛点。

本项目旨在重构工作流的触发、配置、执行与结果交付链路，建立“即用即唤、流式内嵌、业务闭环”的新一代 AI 财务交互体系。

---

## 2. 核心架构与模块划分

```
frontend/src/
├── components/
│   ├── WorkflowBar.vue          // [重构] 紧凑快捷胶囊栏 (36px) + 全量工作流抽屉 + 常用置顶(Pin)
│   ├── WorkflowSlashMenu.vue    // [新增] 输入框键入 "/" 触发的浮动命令建议菜单
│   ├── WorkflowConfigCard.vue   // [新增] 对话流内嵌参数配置卡片 (Inline Form)
│   ├── WorkflowProgressCard.vue // [增强] 步骤流水线 + 细粒度耗时 + 业务行动按钮 + 智能追问气泡
│   └── ChatMessageContent.vue   // [更新] 支持内嵌配置卡片与行动组件的分发渲染
├── stores/
│   └── chat.ts                  // [扩展] 支持内联参数暂存、置顶列表持久化(LocalStorage)
└── views/
    └── Chat.vue                 // [集成] 输入框 "/" 监听、抽屉呼出、业务弹窗预填联动
```

---

## 3. 详细交互设计与技术实现

### 3.1 触发与发现：紧凑胶囊条 + 斜杠指令

1. **紧凑胶囊栏（WorkflowBar.vue）**
   - 默认固定高度 `36px`，位于聊天输入框上方，采用磨砂半透明质感（`rgba(15, 23, 42, 0.7)`）。
   - **左侧**：展示 `⚡ 常用` 标签以及 3-4 个置顶胶囊 Chip，点击直接唤起（无参直接运行，需参插入内联配置卡片）。
   - **右侧**：展示 `全部 13 ▾` 按钮，点击自右侧平滑滑出全量工作流抽屉（Drawer）。
   - **置顶与持久化**：用户在全量抽屉中点击星标 `⭐ Pin/Unpin`，置顶工作流 ID 数组实时持久化到 `localStorage['minefolio_pinned_workflows']`（默认：`wf_monthly_review`, `wf_portfolio_rebalance`, `wf_payday_split`, `wf_budget_guard`）。

2. **斜杠指令菜单（WorkflowSlashMenu.vue）**
   - 在聊天输入框中输入 `/` 或 `/wf` 时自动弹出浮动建议面板。
   - 支持键盘快捷操作：`↑` / `↓` 切换选项，`Enter` 选中确认，`Esc` 退出。
   - 支持拼音首字母、中文名称、英文标识模糊检索。

---

### 3.2 参数配置：对话流内联卡片（WorkflowConfigCard.vue）

1. **交互形态**
   - 替代传统的居中模态弹窗（`el-dialog`），点击需参数的工作流后直接在消息流最新位置插入一张内联配置卡片。
   - 保持对话时间线完整，用户无需跳离上下文。
2. **专属参数控件与快速预设**
   - **月度复盘 / 预算守护**（`wf_monthly_review`, `wf_budget_guard`）：月份选择器（默认当月，提供【上月】/【当月】快捷标签）。
   - **大额支出决策**（`wf_expense_decision`）：拟支出金额（数字步进器 + 常用金额 Chip：1000/5000/10000）。
   - **发薪日资金分流**（`wf_payday_split`）：四维比例分配输入 + 4 种预设模板（稳健型 50/20/20/10、激进型 40/30/20/10、保守型 60/10/20/10、均衡型 35/25/25/15）+ 动态自动归一化。
   - **异常排查 / 订阅审计**（`wf_anomaly_detect`, `wf_subscription_audit`）：回溯天数滑块（默认 60/180 天）。
   - **应急资金 / 现金流预测**（`wf_emergency_fund`, `wf_cashflow_forecast`）：目标覆盖月数 / 预测周期。
3. **无缝执行**
   - 点击【⚡ 启动流水线】后，配置卡片就地原地转换为 `WorkflowProgressCard`，并触发 SSE 流式响应，无需新增多余消息气泡。

---

### 3.3 执行与进度：细粒度耗时与流水线监控（WorkflowProgressCard.vue）

1. **真实耗时与动态 ETA**
   - 记录各步骤的实际执行毫秒数，运行时动态展示单步与总耗时。
2. **流式状态管理**
   - 步骤状态流转：`pending` ➜ `running` ➜ `completed`（或 `error` / `canceled`）。
   - 步骤右侧展示提取的摘要（Summary）。
   - 用户点击 Esc 或【停止生成】时，保留已完成步骤，卡片标记为 `interrupted`，支持从中断步重试。

---

### 3.4 结果交付与业务闭环

1. **业务行动工具条（Contextual Action Bar）**
   - **资产再平衡（wf_portfolio_rebalance）**：提供【📋 一键生成调仓交易单】按钮，点击后直接唤起交易记账对话框，并自动预填资产类型、买卖方向和金额草稿。
   - **预算守护 / 支出决策（wf_budget_guard / wf_expense_decision）**：提供【⚡ 调整分类预算】与【✍️ 快速记一笔】按钮，联动打开分类预算抽屉或记账弹窗。
   - **通用工具**：提供【📄 导出 Markdown 报告】与【📋 一键复制摘要】按钮。
2. **智能追问气泡（Smart Follow-up Prompts）**
   - 工作流完成后，根据分析结论自动展示 2-3 个关联延伸问题气泡（如 *“模拟如果下月削减餐饮 30% 对现金流的影响”*、*“查看此资产近一年的调仓历史”*）。
   - 点击气泡自动将问题填入输入框并直接发送。

---

## 4. 状态流转与边界处理

| 场景 | 处理策略 |
| :--- | :--- |
| **比例总和不等于 100%** | 在 `wf_payday_split` 中，若总和非 100 且非 0，提交时按现有比例自动等比归一化并给予告警提示。 |
| **网络中断 / SSE 错误** | 卡片标红并保留错误原因，重试时自动带入本次输入的参数，无需用户重新填写。 |
| **刷新 / 会话切换** | `workflowData` 与步骤明细保存在 `AiMessage` 中，历史会话加载时默认展示优雅的折叠完成态。 |
| **移动端小屏自适应** | 胶囊栏支持左右单行滚动，全量抽屉自底部以 Bottom Sheet 形式滑出，支持下滑手势关闭。 |

---

## 5. 测试与验证策略

1. **组件单元与交互测试**：
   - 验证 `WorkflowBar.vue` 胶囊点击、Pin/Unpin 持久化到 `localStorage`；
   - 验证输入框 `/` 快捷唤起 `WorkflowSlashMenu.vue` 与键盘上下键导航；
   - 验证 `WorkflowConfigCard.vue` 参数选择、预设比例切换与启动事件；
   - 验证 `WorkflowProgressCard.vue` 各种工作流执行完成后的 Action 按钮触发与追问气泡发送。
2. **构建与集成验证**：
   - 执行 `npm --prefix frontend run build` 确保 TypeScript 类型检查与 Vite 打包 0 错误。
   - 执行 `./tests/test_link.sh` 确保后端工作流接口兼容性。
