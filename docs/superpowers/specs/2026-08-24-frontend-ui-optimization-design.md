# 前端页面全面优化设计规范

**日期**: 2026-08-24
**状态**: 已通过评审并修订
**范围**: AI助手深度交互、AI配置增强与连接测试、全局暗色科技风视觉精修

---

## 1. 概述与目标

通过深化 AI 助手交互能力、扩展 AI 供应商连通性测试与配置体验，结合全局暗色赛博科技风视觉规范升级，全面提升 Minefolio 前端界面的精致度、交互流畅性与专业感。

---

## 2. 详细设计

### 2.1 AI 助手页面优化 (`frontend/src/views/Chat.vue` & `frontend/src/stores/chat.ts`)

1. **快捷财务 Prompts 引导面板**：
   - 当会话消息为空时，展示 4 张快捷卡片：
     - 📊 **月度财务诊断**：*“请分析我本月的收支情况与资产配置健康度”*
     - 💡 **应急储备金规划**：*“根据我的月支出情况，建议如何储备 3-6 个月应急金？”*
     - 📈 **稳健投资策略**：*“简述标准普尔家庭资产象限与定投规划”*
     - 🔍 **开支优化建议**：*“帮我梳理日常非必要开销并提供削减建议”*
   - 点击任一卡片直接填入输入框并发起流式对话。

2. **消息卡片与操作工具栏**：
   - 助手消息右上角悬浮操作条：
     - **复制**：将 Markdown 纯文本复制至剪贴板，弹出轻量成功提示。
     - **重新生成 (Regenerate)**：触发重新生成上一轮回答。前端携带 `regenerate: true`，后端在流式生成前自动清理会话中最后一条 `assistant` 历史消息，防止上下文历史出现重复冗余。
   - **Markdown 排版与安全渲染**：
     - 使用 `marked` 配合严格 HTML 转义与安全过滤，防范 XSS 注入。
     - 代码块：深色背景、语言标签展示、右上角一键复制代码。
     - 表格：斑马纹深色背景、细腻青色边框、数字列靠右对齐。
     - 引用块：左侧 3px 主题色渐变高亮竖条。

3. **会话管理与行内重命名**：
   - 会话列表项悬浮展示「重命名」与「删除」图标。
   - 点击重命名或双击进入行内编辑框，失焦或回车调用 `PUT /api/ai/sessions/:id` 异步保存，并实时响应式更新侧边栏。

4. **输入框与滚动体验**：
   - 采用自适应高度输入框（min 44px, max 160px），支持 `Enter` 发送、`Shift+Enter` 换行。
   - 流式接收期间保持平滑平移到底部，用户主动向上滚动时暂停自动滚动。

---

### 2.2 AI 设置面板增强 (`frontend/src/views/Settings.vue` & 后端 API)

1. **供应商一键测试连接 (`POST /api/settings/ai/test`)**：
   - 供应商卡片中新增「测试连接」按钮。
   - 前端通过 RSA 公钥加密传输当前填写的 `api_key`（若未修改且已有 `has_api_key: true`，则允许缺省并回退使用后端已存储的 Key），`base_url` 保持明文传输。
   - 后端发起轻量级连通性测试，返回统一数据结构：
     ```json
     {
       "success": true,
       "latency_ms": 218,
       "message": "连接成功"
     }
     ```

2. **供应商可用模型后端代理获取 (`POST /api/settings/ai/fetch-models`)**：
   - 增加「获取模型」功能。由于浏览器跨域及 API Key 脱敏限制，由后端作为安全代理转发请求至提供商的 `/models` 端点，提取模型 ID 列表并返回前端。

3. **视觉卡片与状态感知**：
   - 展示供应商凭据状态标签：`API Key 已配置 (传输加密)` / `未设置 API Key`。
   - 密码输入框在已配置状态下展示遮罩占位符 `•••••••• (已加密存储，留空保持不变)`。

---

### 2.3 全局视觉质感与微动效 (`frontend/src/styles/index.css` & `frontend/src/views/Layout.vue`)

1. **暗色科技玻璃拟态**：
   - 卡片与面板采用 `background: var(--mf-surface); backdrop-filter: blur(16px);`。
   - 边框采用动态发光青色微光 `--mf-border-hover: rgba(0, 212, 255, 0.35)`。
2. **微交互与转场**：
   - 路由切换动画：`fade-in-up`（0.3s cubic-bezier）。
   - 按钮点击弹性缩放动效：`scale(0.97)`。
   - 悬浮卡片微升动效：`translateY(-2px)` 及外发光光晕提升。

---

## 3. 接口与数据契约

### 3.1 新增/更新接口

| 接口 | 方法 | 权限 | 请求参数 | 返回结构 | 说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `/api/settings/ai/test` | `POST` | JWT | `{ id, base_url, api_key_enc, model }` | `{ success: boolean, latency_ms: number, message: string }` | 测试供应商连接 |
| `/api/settings/ai/fetch-models` | `POST` | JWT | `{ id, base_url, api_key_enc }` | `{ models: string[] }` | 后端代理拉取模型列表 |
| `/api/ai/sessions/:id` | `PUT` | JWT | `{ title, model }` | `{ code: 0, data: null }` | 更新会话标题或模型 |
| `/api/ai/chat` | `POST` | JWT | `{ session_id, content, model, provider, regenerate }` | SSE Stream (`delta`, `done`, `error`) | 支持重新生成 |
| `/api/settings/ai` | `GET` | JWT | 无 | `{ providers: [{ id, name, base_url, models, has_api_key }], ... }` | 获取配置及脱敏状态 |
| `/api/settings/ai` | `PUT` | JWT | `{ providers: [{ id, name, base_url, models, api_key_enc }], ... }` | `{ code: 0, data: null }` | 保存配置，支持密文传输 |

---

## 4. 验证计划

1. **E2E 交互测试 (Playwright)**：
   - 验证 `/chat` 空状态 4 组 Prompts 快捷发送与流式响应。
   - 验证 Markdown 代码复制、消息重新生成、会话行内重命名与删除。
   - 验证 `/settings` 供应商连接测试（测试成功耗时显示与失败异常捕获）与模型列表获取。
2. **构建与类型验证**：
   - 后端 CMake 编译通过（0 错误）。
   - 前端 TypeScript (`vue-tsc -b`) 及 Vite 构建全通过（0 错误）。
