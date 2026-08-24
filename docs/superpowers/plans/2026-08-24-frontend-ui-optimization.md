# 前端页面全面优化实施计划

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 全面升级 Minefolio 前端 AI 助手交互、AI 供应商设置（含连接测试与模型自动拉取）及全局暗色科技风视觉体验。

**Architecture:** 后端提供轻量连接测试代理与模型拉取代理接口，支持会话消息回滚；前端在 Vue 3 + Pinia + Element Plus 架构下完善 Chat 快捷 Prompts、消息操作栏、行内重命名与 Settings 连接测试，并升级全局 CSS 变量与微动效。

**Tech Stack:** C23 (csilk framework, libcurl, OpenSSL RSA-OAEP), Vue 3, TypeScript, Pinia, Element Plus, Iconify, Marked.

---

## File Structure Map

```
backend/
├── src/
│   ├── controllers/
│   │   └── ai_controller.c/.h          # 新增 test 与 fetch-models 路由处理
│   ├── services/
│   │   └── ai_service.c/.h             # 供应商连通性测试与模型代理业务逻辑
frontend/
├── src/
│   ├── api/
│   │   └── ai.ts                       # 新增 testConnection 与 fetchProviderModels 接口定义
│   ├── stores/
│   │   └── chat.ts                     # 新增会话重命名与重试生成状态动作
│   ├── views/
│   │   ├── Chat.vue                    # 快捷 Prompts 卡片、消息操作栏、行内重命名、自适应输入
│   │   └── Settings.vue                # 供应商测试连接、拉取模型、状态徽章
│   └── styles/
│       └── index.css                   # 玻璃拟态、按钮微动效、Markdown 表格与代码块美化
```

---

## Chunk 1: 后端代理与辅助接口

### Task 1: 供应商连接测试与模型拉取后端接口

**Files:**
- Modify: `backend/src/controllers/ai_controller.h`
- Modify: `backend/src/controllers/ai_controller.c`
- Modify: `backend/src/services/ai_service.h`
- Modify: `backend/src/services/ai_service.c`

- [ ] **Step 1: 在 `ai_service.h` 与 `ai_service.c` 中实现连接测试与模型拉取逻辑**
  - 实现 `ai_service_test_connection(csilk_ctx_t* c)`:
    - 接收 `{ id, base_url, api_key_enc, api_key, model }`
    - 解密 `api_key_enc`（若为空且已有配置，回退使用已持久化的 key）
    - 发起轻量 `/chat/completions` 请求，计算耗时 `latency_ms`
    - 返回 `{ success: true, latency_ms: number, message: "连接成功" }` 或 `{ success: false, latency_ms: 0, message: err }`
  - 实现 `ai_service_fetch_models(csilk_ctx_t* c)`:
    - 接收 `{ id, base_url, api_key_enc, api_key }`
    - 代理调用供应商 `<base_url>/models`，解析提取 `data[].id` 数组并返回 `{ models: string[] }`
  - 增强 `ai_chat_handler` 支持 `regenerate: true`：当收到重新生成标志时，在插入新消息前删除当前会话中的最后一条助手消息。

- [ ] **Step 2: 在 `ai_controller.c` 注册路由**
  - `POST /api/settings/ai/test`
  - `POST /api/settings/ai/fetch-models`

- [ ] **Step 3: 编译后端并验证**
  - 运行 `cmake --build backend/build --parallel`
  - 测试 `curl -X POST http://localhost:8080/api/settings/ai/test` 校验返回值

- [ ] **Step 4: 提交代码**
  - `git commit -m "feat(backend/ai): add connection test, model fetch proxy, and message rollback"`

---

## Chunk 2: 前端 API 与 Store 扩展

### Task 2: 扩展 AI API 与 Chat Store

**Files:**
- Modify: `frontend/src/api/ai.ts`
- Modify: `frontend/src/stores/chat.ts`

- [ ] **Step 1: 在 `frontend/src/api/ai.ts` 中新增接口**
  - `testAiConnection(data)`: 调用 `POST /api/settings/ai/test`
  - `fetchAiModels(data)`: 调用 `POST /api/settings/ai/fetch-models`
  - 在 `chatStream` 入参中增加 `regenerate?: boolean`

- [ ] **Step 2: 在 `frontend/src/stores/chat.ts` 中增加状态方法**
  - `renameSession(id: number, newTitle: string)`: 调用 `PUT /api/ai/sessions/:id` 并更新本地 `sessions` 列表
  - `regenerateLastMessage()`: 找到最后一条 user 提问，将 assistant 回答置空，发起带 `regenerate: true` 的流式请求

- [ ] **Step 3: 验证前端构建与类型**
  - 运行 `npm --prefix frontend run build`

- [ ] **Step 4: 提交代码**
  - `git commit -m "feat(frontend/ai): add test connection API and chat store extensions"`

---

## Chunk 3: AI 助手交互与 Markdown 深度排版

### Task 3: 改造 `Chat.vue` 与全局样式

**Files:**
- Modify: `frontend/src/views/Chat.vue`
- Modify: `frontend/src/styles/index.css`

- [ ] **Step 1: 编写快捷 Prompts 卡片组件**
  - 在空消息状态渲染 2x2 网格，提供财务体检、应急储备金、稳健投资、开支优化 4 个卡片。
  - 点击卡片直接填入输入框并触发 `sendMessage`。

- [ ] **Step 2: 消息操作栏与 Markdown 安全排版**
  - 助手消息增加悬浮操作条：一键复制文本、重新生成按钮。
  - 优化 Markdown 渲染样式：深色代码框、右上角代码复制按钮、表格斑马纹、数字靠右。

- [ ] **Step 3: 会话行内重命名与输入框自适应**
  - 侧边栏会话项增加编辑按钮与双击改名交互。
  - 输入框支持随文本增多平滑自适应高度（max 160px）。

- [ ] **Step 4: 验证构建**
  - 运行 `npm --prefix frontend run build`

- [ ] **Step 5: 提交代码**
  - `git commit -m "feat(frontend/chat): add quick prompts, message action bar, and inline rename"`

---

## Chunk 4: AI 设置页面连通测试与视觉优化

### Task 4: 升级 `Settings.vue` 供应商面板

**Files:**
- Modify: `frontend/src/views/Settings.vue`

- [ ] **Step 1: 供应商配置卡片增加「测试连接」按钮**
  - 按钮支持 loading 状态，测试后在卡片显示连接耗时徽章（如 `🟢 218ms` 或 `🔴 连接失败`）。

- [ ] **Step 2: 供应商配置卡片增加「获取模型」按钮**
  - 点击后代理调用获取模型列表并自动填入 `models` 输入框。

- [ ] **Step 3: 优化密码框与状态标签展示**
  - 区分已配置/未配置状态，遮罩占位符清晰说明。

- [ ] **Step 4: 验证构建**
  - 运行 `npm --prefix frontend run build`

- [ ] **Step 5: 提交代码**
  - `git commit -m "feat(frontend/settings): add connection testing and model auto-fetch in UI"`

---

## Chunk 5: 全链路端到端验证 (Playwright)

### Task 5: 真实浏览器 E2E 验证与截图留存

- [ ] **Step 1: 启动后台与 Qwen 真实服务进行全流程验证**
- [ ] **Step 2: 验证 AI 设置页面「测试连接」与「获取模型」功能**
- [ ] **Step 3: 验证 AI 助手快捷 Prompts、流式回复、复制、重新生成及会话改名**
- [ ] **Step 4: 留存截图至 `output/playwright/` 并交付验证报告**
