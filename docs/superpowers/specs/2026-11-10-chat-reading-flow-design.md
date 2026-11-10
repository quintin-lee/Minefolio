# 流式生成与思考/工具链阅读体验重构设计规范

**日期**: 2026-11-10  
**模块**: `frontend/src/components/ChatMessageContent.vue`  
**目标**: 重构 AI 对话中的深度思考卡片（Thinking Card）与工具调用卡片（Tool Calls Card），消除长思考过程霸屏与多工具堆叠引起的视口剧烈跳动，实现思考中微滚动跟随、正文吐字后优雅收敛、以及工具链步骤时间轴（Steps Timeline）紧凑展示。

---

## 1. 现状分析与痛点

1. **深度思考卡片（Thinking Card）**：
   - 展开状态下默认全文展现（最大高度 400px），但在正文开始输出后依然保持展开，导致上千字推理过程将正文严重挤压至可视区域外；
   - 在思考流式生成阶段，容器内部缺乏随输出内容自底向上平滑滚动的能力，用户看不到最新吐出的思考句子。
2. **工具调用卡片（Tool Calls Card）**：
   - 工具调用以层叠的大块代码块展示，直接平铺 `pre` JSON 格式参数与返回值，如果一次对话连续调用 3~4 个工具，卡片高度直接增加上千像素，导致整个聊天窗口高度剧烈抖动；
   - 缺少工具名称的友好语义化展示，难以一眼看出 AI 正在执行哪一步分析。

---

## 2. 详细交互设计与规范

### 2.1 深度思考卡片重构 (`.thinking-card`)
- **高度约束与微滚动**：
  - 展开主体 `.thinking-content-body` 设置舒适的 `max-height: 220px; overflow-y: auto;`；
  - 自定义极细滚动条并添加底部向上渐变遮罩（`::after` 渐变遮罩），提示用户内容可向下滚动。
- **思考中实时滚动跟随**：
  - 监听 `thinkingContent` 变化，当 `isThinkingStreaming` 为真时，通过 `nextTick` 平滑将 `.thinking-content-body` 滚动到 `scrollHeight`。
- **正文开始输出时平滑收敛**：
  - 监听 `props.content`：当 `content` 开始由空变有且用户在此会话中未曾手动干预展开时，将 `isThinkingExpanded` 自动收起为紧凑状态，状态文本由“深度思考中...”变为“已深度思考 (Xs)”，释放垂直空间给正文阅读；用户需要回顾时可随时点击展开。

### 2.2 工具链步骤时间轴指示器 (`.tool-card` / Steps Timeline)
- **工具名称语义化映射表**：
  ```ts
  const TOOL_NAME_MAP: Record<string, { label: string; icon: string }> = {
    query_assets: { label: '查询持仓资产', icon: 'ph:vault-bold' },
    list_assets: { label: '获取资产列表', icon: 'ph:vault-bold' },
    sync_quotes: { label: '同步市场行情', icon: 'ph:trend-up-bold' },
    fetch_market_index: { label: '查询大盘指数', icon: 'ph:chart-line-bold' },
    propose_daily_expense: { label: '生成收支记账', icon: 'ph:receipt-bold' },
    propose_transfer: { label: '生成资金调拨', icon: 'ph:arrows-left-right-bold' },
    analyze_financial_health: { label: '财务健康体检', icon: 'ph:heartbeat-bold' },
    web_search: { label: '全网实时检索', icon: 'ph:globe-bold' },
    execute_workflow_step: { label: '执行工作流步骤', icon: 'ph:git-merge-bold' },
  }
  ```
- **头部紧凑步骤预览**：
  - 在 `.tool-card-header` 中展示步骤胶囊链概览（如 `① 查询资产 ✓  ② 汇率同步 ✓`）；
  - 正在执行的步骤展示转圈脉冲指示器。
- **折叠式步骤详情 (Accordion)**：
  - 展开 `.tool-card-body` 后，以时间轴节点（Timeline Step Item）展现每个工具；
  - 默认展示步骤名、耗时/状态标签与展开详情按钮；
  - 点击单个步骤展开该步骤的参数和结果，支持一键复制代码，不再同时平铺所有工具的全部原始 JSON。

---

## 3. 影响文件与修改范围
- `frontend/src/components/ChatMessageContent.vue`: 模板重构、逻辑增强、CSS 动画升级。
- `frontend/tests/chat-message-thinking.spec.ts`: 更新深度思考卡片断言（220px 舒适高度、正文就绪自动收起）。
- `frontend/tests/chat-message-tool-card.spec.ts`: 更新工具链时间轴步骤断言（语义化映射、单步详情折叠）。
