# AI 思考模式 (Thinking / Reasoning Stream & Persistence) 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 实现大模型思考模式（Reasoning / Thinking Stream），支持解析 OpenAI 兼容流式思考分块并通过 SSE `event: thinking` 分发到前端，以 ChatGPT/Claude.ai 折叠卡片形式实时呈现，并将完整思考过程持久化至数据库。

**Architecture:** 
1. 在 `deps/csilk` 驱动层扩展 OpenAI SSE 解析器，捕获 `reasoning_content` 并触发 `on_reasoning_chunk` 回调；
2. 在 Minefolio AI 统一运行时 (`loop.c`) 中累积思考分块，通过 `ai_service.c` 桥接向前端分发 `event: thinking` 事件，并存入 `ai_messages.thinking_content`；
3. 数据库新增 `V015` 迁移（SQLite + PostgreSQL）；
4. 前端 Pinia Store (`chat.ts`) 消费分块，`ChatMessageContent.vue` 呈现思考中实时展开打字、正文输出后自动收起与历史折叠面板。

**Tech Stack:** C23 (GCC 14), CMake, SQLite 3 / PostgreSQL (libpq), Vue 3, TypeScript, Pinia, Element Plus, Iconify.

**Spec:** `docs/superpowers/specs/2026-10-21-ai-thinking-mode-design.md`

## Global Constraints

- Backend C23 standard with GNU extensions, CMake with Unix Makefiles only (never Ninja).
- DDD four-layer architecture integrity: domain layer zero dependencies, repos only in `infrastructure/repositories/`.
- Dual database compatibility: every migration and repo query must work seamlessly on both SQLite and PostgreSQL.
- Frontend: TypeScript strict mode, `vue-tsc -b` must pass with 0 errors.
- CSS: Follow `--mf-*` theme design tokens.

## Review Focus

1. **多轮工具调用时思考链累积**：在 Agent 循环中多次推理与工具调用时，思考内容需累积保存，且回传给下轮大模型的 `msgs` 中绝不可携带 `reasoning_content`，避免 API 400 校验失败。
2. **多轮历史对话上下文隔离**：`mf_ai_message_recent` 和 `ai_memory_build_messages` 仅提取 `content`，绝不可注入 `thinking_content` 到 prompt，防止上下文窗口耗尽。
3. **流式阶段中断与断网保护**：用户在思考阶段点击“停止生成”或遇网络中断时，已产生的思考内容仍持久化，正文为 `[思考已中断]`，前端操作栏保持可用可重新生成。
4. **历史记录重载一致性**：`getMessages` 分页加载与 `chat.ts` 转换必须提取 `thinking_content`，确保刷新页面后历史思考折叠块依然完整展示。
5. **非思考模型向后兼容**：不返回 `reasoning_content` 的模型（如普通 GPT-4o、旧版模型）正常走 `event: delta` 正文流，不产生空思考块。

---

### Task 1: Csilk AI 驱动层支持 Reasoning Chunk 解析

**Files:**
- Modify: `deps/csilk/include/csilk/drivers/ai.h:120-150`
- Modify: `deps/csilk/src/drivers/ai/ai.c:770-795`
- Modify: `deps/csilk/src/drivers/ai/openai.c:180-210, 570-605`
- Test: `deps/csilk/tests/drivers/test_openai_mock.c`

**Interfaces:**
- Consumes: OpenAI SSE 格式中 `choices[0].delta.reasoning_content` 与 `delta.reasoning`
- Produces: `req->on_reasoning_chunk(chunk, req->user_data)` 及 `res->reasoning_content`

- [x] **Step 1: 扩展 csilk AI 头文件定义**
  在 `deps/csilk/include/csilk/drivers/ai.h` 中：
  - 在 `csilk_ai_chat_request_t` 结构体新增 `csilk_ai_stream_cb on_reasoning_chunk;`
  - 在 `csilk_ai_chat_response_t` 结构体新增 `char* reasoning_content;`
  在 `deps/csilk/src/drivers/ai/ai.c` 的 `csilk_ai_chat_response_free` 函数中新增 `free(res->reasoning_content);`。

- [x] **Step 2: 在 OpenAI 驱动中增加流式与非流式 reasoning 解析**
  在 `deps/csilk/src/drivers/ai/openai.c` 中：
  - 流式路径 (`process_stream_line`)：在处理 `delta` 时，读取 `delta.reasoning_content`（及 `delta.reasoning` 回退），若非空字符串，追加到 `ctx->res->reasoning_content` 并调用 `ctx->req->on_reasoning_chunk`。
  - 完成检查路径 (`openai_chat` step 4a)：将流式完成判断条件扩展为 `(res->content != NULL || res->reasoning_content != NULL || res->tool_call_count > 0)`，防止仅有思考内容时被判定失败。
  - 非流式路径 (`openai_chat` step 4b)：从 `choices[0].message` 中读取 `reasoning_content` / `reasoning`，赋值给 `res->reasoning_content = strdup(...)`。

- [x] **Step 3: 运行 csilk 驱动测试验证**
  Run: `cmake --build backend/build --target test_ai_runtime test_ai_tools`
  Expected: 编译通过且无警告。

- [x] **Step 4: Commit**
  ```bash
  git -C deps/csilk add include/csilk/drivers/ai.h src/drivers/ai/ai.c src/drivers/ai/openai.c
  git -C deps/csilk commit -m "feat(ai): support reasoning_content streaming and response in OpenAI driver"
  ```

---

### Task 2: 数据库模式迁移 V015 与仓储层持久化适配

**Files:**
- Create: `backend/sql/migrations/sqlite/V015__ai_messages_thinking_content.sql`
- Create: `backend/sql/migrations/postgres/V015__ai_messages_thinking_content.sql`
- Modify: `backend/sql/migration.sql:173-183`
- Modify: `backend/sql/migration_postgres.sql:173-183`
- Modify: `backend/src/infrastructure/repositories/ai_session_repo_impl.h:27-37`
- Modify: `backend/src/infrastructure/repositories/ai_session_repo_impl.c:160-222`
- Modify: `backend/src/services/ai/runtime/session.c:63-75`
- Modify: `backend/src/services/ai/workflow/executor.c:140-146`
- Modify: `backend/src/services/ai_service.c:470-478`
- Test: `backend/tests/unit/test_domain_ai.c`

**Interfaces:**
- Consumes: `ai_messages` 表结构
- Produces: 
  - `int64_t mf_ai_message_insert(csilk_db_pool_t* pool, int64_t session_id, const char* role, const char* content, const char* model, const char* thinking_content)`
  - `mf_ai_message_list` 返回字段包含 `thinking_content`

- [x] **Step 1: 编写数据库迁移脚本与基线更新**
  - 在 `backend/sql/migrations/sqlite/V015__ai_messages_thinking_content.sql` 编写：
    ```sql
    -- V015: ai_messages 增加 thinking_content 列存储大模型思考链过程
    ALTER TABLE ai_messages ADD COLUMN thinking_content TEXT DEFAULT '';
    ```
  - 在 `backend/sql/migrations/postgres/V015__ai_messages_thinking_content.sql` 编写相同 SQL。
  - 在 `backend/sql/migration.sql` 与 `backend/sql/migration_postgres.sql` 的 `CREATE TABLE IF NOT EXISTS ai_messages` 中新增 `thinking_content TEXT DEFAULT '',`。

- [x] **Step 2: 改造仓储实现 `ai_session_repo_impl.c/.h`**
  - 在 `ai_session_repo_impl.h` 将 `mf_ai_message_insert` 签名扩展为增加 `const char* thinking_content` 参数。
  - 在 `ai_session_repo_impl.c` 中：
    - `mf_ai_message_list`: 查询语句修改为 `SELECT id, session_id, role, content, thinking_content, model, created_at FROM ai_messages ...`。
    - `mf_ai_message_insert`: 插入语句增加 `thinking_content` 列，传参绑定 `thinking_content ? thinking_content : ""`。
  - 同步更新既有调用点默认传 `NULL` 或 `""`：
    - `backend/src/services/ai/runtime/session.c:73` (传入 `NULL`)
    - `backend/src/services/ai/workflow/executor.c:142` (传入 `NULL`)
    - `backend/src/services/ai_service.c:475` (用户消息传入 `NULL`)

- [x] **Step 3: 编译与执行迁移单测**
  Run: `cmake --build backend/build --target test_migration_engine test_database_repository && ./backend/build/test_migration_engine`
  Expected: PASS

- [x] **Step 4: Commit**
  ```bash
  git add backend/sql/migrations/ backend/sql/migration.sql backend/sql/migration_postgres.sql backend/src/infrastructure/repositories/ai_session_repo_impl.* backend/src/services/ai/runtime/session.c backend/src/services/ai/workflow/executor.c backend/src/services/ai_service.c
  git commit -m "feat(db): add V015 migration for ai_messages thinking_content and update repository"
  ```

---

### Task 3: 后端 AI 运行时 Agent 循环与 SSE 桥接支持思考流

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.h:15-30`
- Modify: `backend/src/services/ai/runtime/loop.c:20-60, 200-265, 380-445`
- Modify: `backend/src/services/ai_service.c:210-290, 540-560`
- Test: `backend/tests/unit/test_ai_runtime.c`

**Interfaces:**
- Consumes: `csilk_ai_chat_request_t.on_reasoning_chunk`
- Produces: 
  - `ai_runtime_callbacks_t.on_thinking_chunk`
  - SSE 事件 `event: thinking` 带有 `{"content": "...", "timestamp": ..., "time": "..."}`
  - 持久化 `thinking_accumulated` 到数据库

- [x] **Step 1: 编写 runtime 单元测试用例**
  在 `backend/tests/unit/test_ai_runtime.c` 中新增测试用例：
  - 验证 `ai_runtime_callbacks_t` 结构体包含 `on_thinking_chunk` 字段；
  - 验证带思考内容的模型交互能正确推进并统计。

- [x] **Step 2: 改造 `loop.h` 与 `loop.c`**
  - 在 `services/ai/runtime/loop.h` 的 `ai_runtime_callbacks_t` 中新增：
    `void (*on_thinking_chunk)(const char* chunk, void* udata);`
  - 在 `services/ai/runtime/loop.c` 中：
    - `loop_stream_bridge_t` 结构新增 `char* thinking_accumulated`, `size_t thinking_accumulated_len`, `size_t thinking_accumulated_cap`。
    - 实现 `on_reasoning_bridge` 回调：动态扩容并累积字符串，且触发 `cbs->on_thinking_chunk`。
    - 构建 `csilk_ai_chat_request_t req` 时挂载 `.on_reasoning_chunk = on_reasoning_bridge`。
    - 在单轮/多轮结束持久化入库处（第 253 行及 389 行附近），获取 `final_thinking`，调用 `mf_ai_message_insert(pool, ctx->session_id, "assistant", final_text, model, final_thinking)`。
    - 在异常/中断分支（第 387 行），若 `!got_text` 但 `thinking_accumulated` 有内容，将正文置为 `[思考已中断]` 并持久化思考内容。
    - 在中间轮次追加 `assistant` 消息给下一次大模型请求（`msgs`）时，确保不带 `reasoning_content`，只带 `content` 和 `tool_calls`。
    - 在资源清理部分释放 `bridge.thinking_accumulated`。

- [x] **Step 3: 改造 `services/ai_service.c` 实现 SSE 思考事件桥接**
  - 实现 `sse_bridge_thinking_chunk(const char* delta, void* udata)`：
    - 初始化 SSE 连接（若尚未初始化）；
    - 记录首 Token 到达时间 `ai_trace_record_first_token(b->trace)`；
    - 发送 SSE 事件 `csilk_sse_send(b->ctx, "thinking", target_buf)`。
  - 在 `ai_chat_handler` 中将 `cbs.on_thinking_chunk = sse_bridge_thinking_chunk`。

- [x] **Step 4: 编译与运行后端全套单元测试**
  Run: `cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
  Expected: 所有测试 100% 通过。

- [x] **Step 5: Commit**
  ```bash
  git add backend/src/services/ai/runtime/loop.* backend/src/services/ai_service.c backend/tests/unit/test_ai_runtime.c
  git commit -m "feat(ai): integrate thinking reasoning stream and persistence in AI runtime"
  ```

---

### Task 4: 前端 API 类型定义与 Chat Store 流式消费更新

**Files:**
- Modify: `frontend/src/api/ai.ts:44-65, 184-198, 270-295`
- Modify: `frontend/src/stores/chat.ts:205-225, 350-370, 410-425, 470-490`
- Create: `frontend/src/test/chat-thinking.spec.ts`

**Interfaces:**
- Consumes: 后端 SSE `event: thinking`
- Produces: `assistantMsg.thinking_content` 响应式状态更新，以及历史加载时的字段映射

- [x] **Step 1: 编写前端 Store 测试用例**
  创建 `frontend/src/test/chat-thinking.spec.ts`：
  - 测试 `streamChat` 遇到 `event: thinking` 时产出 `{ type: 'thinking', content: '...' }`；
  - 测试 `chat.sendMessage` 收到 thinking chunk 时正确累加到当前 assistant 消息的 `thinking_content` 属性。

- [x] **Step 2: 扩展 `frontend/src/api/ai.ts` 类型与 SSE 生成器**
  - 在 `AiMessage` 接口增加 `thinking_content?: string`。
  - 在 `ChatStreamChunk` 接口将 `type` 扩展为 `'delta' | 'thinking' | 'done' | 'error'`。
  - 在 `streamChat` 生成器函数中支持 `currentEvent === 'thinking'` 的分块产出。

- [x] **Step 3: 改造 `frontend/src/stores/chat.ts`**
  - 在 `loadMoreMessages` 历史消息映射中，保留 `thinking_content: m.thinking_content ? String(m.thinking_content) : undefined`。
  - 在 `sendQuickMessage`、`regenerateLastMessage`、`sendMessage` 中处理 chunk：
    ```ts
    if (chunk.type === 'thinking' && chunk.content) {
      if (!assistantMsg.thinking_content) {
        assistantMsg.thinking_content = ''
      }
      assistantMsg.thinking_content += chunk.content
    } else if (chunk.type === 'delta' && chunk.content) {
      writer.push(chunk.content)
    }
    ```

- [x] **Step 4: 运行前端单元测试**
  Run: `npm --prefix frontend test`
  Expected: PASS

- [x] **Step 5: Commit**
  ```bash
  git add frontend/src/api/ai.ts frontend/src/stores/chat.ts frontend/src/test/chat-thinking.spec.ts
  git commit -m "feat(frontend): support thinking chunk consumption and persistence mapping in chat store"
  ```

---

### Task 5: 前端折叠思考卡片组件与页面交互集成

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue:1-130, 600-680`
- Modify: `frontend/src/views/Chat.vue:210-245, 1600-1640`
- Test: `frontend/src/test/ChatMessageContent.spec.ts`

**Interfaces:**
- Consumes: `:thinking-content="msg.thinking_content"`
- Produces: 极简折叠式思考面板，支持流式自动展开打字、完成后折叠与历史重载渲染

- [x] **Step 1: 编写组件单测**
  在 `frontend/src/test/ChatMessageContent.spec.ts` 中新增测试用例：
  - 当传入 `thinkingContent` 且非空时，渲染思考卡片折叠面板；
  - 点击头部可切换展开/收起状态；
  - 提供复制思考内容功能。

- [x] **Step 2: 改造 `ChatMessageContent.vue`**
  - 在 `props` 中新增 `thinkingContent?: string`。
  - 维护内部状态 `isThinkingExpanded = ref(false)`，思考开始时间与耗时计时。
  - 添加 watch 逻辑：
    - 当处于流式中且正文尚未产生（`props.isStreaming && !props.content && props.thinkingContent`）时，`isThinkingExpanded.value = true`（实时展开思考）；
    - 当正文到达（`props.content` 产生）或流式结束时，自动收起 `isThinkingExpanded.value = false`（用户亦可随时点击重新展开）。
  - 在 template 顶部（工作流卡片之后、segments 之前）添加思考卡片结构：
    - 头部：大脑/火花图标 (`ph:sparkle-bold`)、状态文案（思考中 / 已深度思考）、耗时标签、一键复制按钮、折叠箭头；
    - 内容区：展开时展示带有平滑滚动的 pre-wrap 文本或轻量 Markdown 格式。
  - 添加与主题 `--mf-*` 一致的精致 CSS 样式。

- [x] **Step 3: 改造 `Chat.vue`**
  - 修改旧三点跳动静态占位的 `v-if` 条件：
    `v-if="!msg.content && chat.isStreaming && !msg.workflowData && !msg.workflowConfig && !msg.thinking_content"`
    （确保一旦思考内容到达，立即转由 `ChatMessageContent` 渲染实时思考卡片）。
  - 在 `<ChatMessageContent>` 调用处传入 `:thinking-content="msg.thinking_content"`。
  - 将操作栏（复制、重新生成）的显示条件扩展为：
    `v-if="(msg.content || msg.thinking_content) && !chat.isStreaming"`。

- [x] **Step 4: 前端类型检查与构建验证**
  Run: `npm --prefix frontend run build`
  Expected: `vue-tsc -b` 0 errors, vite build 成功。

- [x] **Step 5: Commit**
  ```bash
  git add frontend/src/components/ChatMessageContent.vue frontend/src/views/Chat.vue frontend/src/test/ChatMessageContent.spec.ts
  git commit -m "feat(ui): implement collapsible thinking card in ChatMessageContent and integrate with Chat view"
  ```

---

### Task 6: 全链路端到端回归验证与集成测试

**Files:**
- Test: 全系统编译与自动化测试套件

- [x] **Step 1: 运行完整后端编译与全套 CTest 测试**
  Run: `cmake -B backend/build -G "Unix Makefiles" && cmake --build backend/build --parallel && ctest --test-dir backend/build --output-on-failure`
  Expected: 35+ 个单元测试套件全部 PASS。

- [x] **Step 2: 运行前端全量单元测试与类型检查**
  Run: `npm --prefix frontend test && npm --prefix frontend run build`
  Expected: 全部前端测试 PASS，类型检查 0 报错。

- [x] **Step 3: 运行全量集成测试脚本**
  Run: `./backend/tests/test_link.sh`
  Expected: 39-case 151+ assertions 全部 PASS。

- [x] **Step 4: 最终提交与分支状态核查**
  ```bash
  git status
  ```
  Expected: 工作区干净无未提交残留文件。
