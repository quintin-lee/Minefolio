# AI 对话页面顶部工具栏瘦身与信息聚合设计方案

**日期**: 2026-11-09  
**模块**: `frontend/src/views/Chat.vue`  
**目标**: 简化 AI 对话页面的顶部工具栏，消除重复控件与拥挤平铺的按钮，建立“信息中枢工作台（Context Hub）”，聚合会话标题快速重命名、整场会话 Token/开销度量指标与紧凑更多菜单。

---

## 1. 现状痛点分析

在现有的 `frontend/src/views/Chat.vue` 中，顶部工具栏（`.chat-toolbar`）存在以下问题：
1. **控件冗余严重**：
   - 顶部工具栏中存在 `toolbar-thinking-btn`（深度思考开关），而输入框底部常驻栏也存在 `input-thinking-btn`，重复且容易引起视觉疲劳；
   - 模型选择器（`el-select`）选中后，旁边紧接着一个 `.current-model-badge` 显示相同名称；
   - 右侧直接平铺了【新对话】、【导出】、【复制】、【清空】四个按钮，在中小屏（1280~1440px）笔记本中与展开的侧栏和分析面板发生剧烈空间争抢。
2. **核心元信息缺失**：
   - 缺少当前激活会话标题显示：用户在历史多轮会话之间切换或折叠侧栏时，无法直接得知当前所处的会话主题；
   - 缺少整场会话的 Token 与成本统计：目前 token/cost 仅散落在单条 assistant 消息底部，用户无法在顶栏快速了解本场会话的综合开销。

---

## 2. 总体设计：信息中枢工作台 (Context Hub)

### 2.1 布局架构

```text
+-------------------------------------------------------------------------------------------------------------------------+
| [展开侧栏] [提供商/模型选择胶囊 ▼] [● 生成中 (停止)]  |  [会话标题 ✏️ (双击/点击快速重命名)]  |  [⚡ 3,420 · $0.0051] [+ 新对话] [✨ 2] [··· 更多 ▼] |
+-------------------------------------------------------------------------------------------------------------------------+
```

### 2.2 模块细化

#### 1. 左侧区域 (`.toolbar-left`)
- **侧栏展开按钮**（`toggle-sidebar-btn`）：当侧栏折叠时显示；
- **紧凑模型选择胶囊**（`model-selector-wrap`）：保留 `selectedModelKey` 下拉选择器，优化其外观为轻量级胶囊样式，移除重复的 `.current-model-badge`；
- **移除重复的深度思考开关**：移除 `.toolbar-thinking-btn`，统一由输入框底部的开关控制；
- **流式状态指示器**（`chat-status`）：在 `chat.isStreaming` 为 true 时展示脉冲红点与 `停止 (Esc)` 按钮。

#### 2. 中间区域 (`.toolbar-center`)
- **当前会话标题显示与原地重命名**：
  - `currentSessionTitle`：读取当前激活会话的 `title`（若无激活会话或新会话，则显示“新对话”）；
  - 支持悬停显示铅笔图标，双击标题或点击铅笔进入内联编辑状态；
  - 聚焦输入框，按 `Enter` 或失焦自动保存（调用 `chat.renameSession(id, title)`），按 `Esc` 取消编辑；
  - 样式设置最大宽度与 `text-overflow: ellipsis`，防止标题过长溢出。

#### 3. 右侧区域 (`.toolbar-right`)
- **会话开销指示器**（`.session-stat-pill`）：
  - 计算属性 `sessionMetrics` 遍历 `chat.messages`，累计 `total_tokens` 与 `cost_usd`；
  - 格式：`⚡ {tokens} · ${cost}`，带 tooltip 详细提示；当 tokens 为 0 时隐藏；
- **分析面板快捷开关**（`.toolbar-panel-btn`）：
  - 当 `panelItems.length > 0` 且在桌面端时显示，带有当前分析卡片总数徽标（如 `✨ 2`），支持快速展开/收起右侧 AnalysisPanel；
- **新对话按钮**（`.new-chat-btn`）：
  - 保留高频快捷操作，提供图标与文字（移动端仅图标）；
- **更多操作下拉菜单**（`.more-actions-dropdown`）：
  - 使用 `el-dropdown`，触发按钮为极简三点图标 `···`；
  - 包含菜单项：
    1. **导出 Markdown** (`exportToMarkdown`, 图标 `ph:download-simple`)
    2. **复制对话记录** (`copyAllChat`, 图标 `ph:copy`)
    3. **AI 审计追踪** (跳转 `/ai-traces`, 图标 `ph:chart-line-up`)
    4. **清空本场记录** (`handleClearChat`, 图标 `ph:trash`, 危险操作提示，标红)

---

## 3. 响应式规则 (Responsive Design)

- **桌面宽屏 (> 1024px)**：完整展示所有部件；
- **平板 / 窄屏 (768px ~ 1024px)**：
  - 隐藏 `.session-stat-pill`；
  - `.toolbar-center` 标题限宽 160px；
  - `.new-chat-btn` 隐藏文本，仅显示 `+` 图标；
- **移动端 (< 768px)**：
  - 隐藏中间会话标题；
  - 仅保留模型选择、新对话图标与 `···` 更多操作菜单。

---

## 4. 影响文件与修改点

- `frontend/src/views/Chat.vue`:
  - 模板更新：重构 `.chat-toolbar`（移除重复项，新增 `.toolbar-center`，右侧替换为 `sessionMetrics` 胶囊与 `el-dropdown`）；
  - 逻辑更新：添加 `sessionMetrics` 统计、顶部标题内联重命名状态机（`editingTopTitle`, `topRenameText`）；
  - 样式更新：为 `.chat-toolbar`, `.toolbar-center`, `.session-stat-pill`, `.toolbar-panel-btn` 编写响应式与微交互 CSS。
- 单元测试：`frontend/tests/unit/chat-toolbar.spec.ts` 锁定度量统计、重命名与下拉菜单功能。
