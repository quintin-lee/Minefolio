# 移动端专用 AI 聊天页实现计划 (Mobile Chat Page Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增专用 `ChatMobile.vue` 替换 `/m/chat` 对桌面 `Chat.vue` 的复用，复用 `useChatStore` 保持与桌面一致的数据层，并加入本地草稿持久化，使断网/切后台/重载后输入内容不丢失。

**Architecture:**
1. **页面隔离**：`/m/chat` 指向 `views-mobile/ChatMobile.vue`；桌面 `/chat` 与 `Chat.vue` 不变。
2. **数据复用**：直接使用 `useChatStore`（sessions/messages/isStreaming/sendMessage/abortCurrentStream）；消息渲染复用 `ChatMessageContent.vue`，空态复用 `PromptStarters.vue`。
3. **草稿存储**：本地 SQLite 新增 `chat_drafts` 表（`scope_key` 主键、`content`、`updated_at`），由 `utils/chat-draft.ts` 提供 `loadDraft`/`saveDraft`/`clearDraft`；不进入 `sync_queue`，不上传服务端。
4. **成功判定**：`doSendMessage` 将流式异常写入 assistant 消息的 `error` 字段且不 reject（`stores/chat.ts:798-804`），因此草稿清理必须等 `isStreaming` 归 false 后检查该字段，而非依赖 `await` 是否抛错。

**Tech Stack:** Vue 3, Pinia, TypeScript, sql.js, Vitest, Playwright.

**Spec:** `docs/superpowers/specs/2026-11-26-mobile-chat-page-design.md`（spec-review 已修订）。

## Global Constraints

- 不修改桌面 `Chat.vue` 与 `views/Chat.vue` 的路由；仅改 `router/mobile.ts` 中 `/m/chat` 的 component。
- `chat_drafts` 不注册到 `SyncEngine`，不入 `sync_queue`，不产生冲突项；登出时随 `resetLocalDb()` 清空（`stores/auth.ts:252`）。
- 工作流入口（`WorkflowBar`/`WorkflowSlashMenu`）不在本计划范围内。
- 离线时不调用 `chat.sendMessage()`，不实现"离线假回复"；网络恢复后不自动发送草稿。
- 消息列表滚动只监听最后一条消息 ID，不监听 `messages.length`（AGENTS.md 第 12 条）。

## Review Focus

1. 草稿在重载本地库后仍存在；切换会话时旧 scope 已保存、新 scope 已加载。
2. 发送后 scope 从 `new` 变为 `session:<id>` 时，两个 scope 的草稿都被正确清理或保留。
3. 流式失败时草稿保留且错误可见；流式期间不能重复发送、不清草稿。
4. 桌面 `/chat` 回归无变化。

---

## Task 1: 草稿表与工具模块

**Files:**
- Modify: `frontend/src/db/schema.ts`
- Create: `frontend/src/utils/chat-draft.ts`
- Create: `frontend/tests/chat-draft.spec.ts`

**Details:**
1. `LOCAL_SCHEMA` 追加：
   ```sql
   CREATE TABLE IF NOT EXISTS chat_drafts (
     scope_key  TEXT PRIMARY KEY,
     content    TEXT NOT NULL,
     updated_at TEXT NOT NULL
   );
   ```
   新表用 `CREATE TABLE IF NOT EXISTS` 即可，无需 ALTER 迁移。
2. `utils/chat-draft.ts`：
   ```ts
   export function loadDraft(scopeKey: string): string
   export function saveDraft(scopeKey: string, content: string): void
   export function clearDraft(scopeKey: string): void
   ```
   `saveDraft` 用 `INSERT ... ON CONFLICT(scope_key) DO UPDATE SET content=?, updated_at=?`，写后 `persist()`；`clearDraft` delete + `persist()`；`loadDraft` 无记录返回 `''`。
3. 单测：基本读写/upsert/clear；`persist()` 后模拟重载模块（参考 `db.local.spec.ts` 的 `reloadDb()`）草稿仍在。

- [ ] **Step 1: 加 `chat_drafts` 建表语句**
- [ ] **Step 2: 实现 `utils/chat-draft.ts`**
- [ ] **Step 3: 编写并运行 `chat-draft.spec.ts`**

---

## Task 2: ChatMobile 页面与路由

**Files:**
- Create: `frontend/src/views-mobile/ChatMobile.vue`
- Modify: `frontend/src/router/mobile.ts:36`

**Details:**
1. 页面结构：header（会话标题/新建/会话列表按钮）、会话抽屉、消息列表（`ChatMessageContent`）、底部输入栏（`textarea` + 发送按钮）、空态（`PromptStarters`）。
2. 数据层：`const chat = useChatStore()`；`onMounted` 调用 `chat.fetchSessions()`；会话切换 `chat.selectSession(id)`；发送 `chat.sendMessage(text)`。
3. 流式期间禁用发送按钮并展示流式占位；滚动策略只 watch 最后一条消息 ID。
4. `router/mobile.ts:36` 的 component 改为 `@/views-mobile/ChatMobile.vue`，路由名 `/m/chat` 不变；底部 Tab 与 Dashboard 快捷入口无需改动。

- [ ] **Step 1: 新建 `ChatMobile.vue` 静态结构与样式**
- [ ] **Step 2: 接入 `useChatStore` 会话/消息/发送/流式**
- [ ] **Step 3: 修改 `/m/chat` 路由并验证 typecheck**

---

## Task 3: 草稿生命周期接入

**Files:**
- Modify: `frontend/src/views-mobile/ChatMobile.vue`
- Modify: `frontend/tests/chat-draft.spec.ts`

**Details:**
1. `inputText` 初值 `loadDraft(currentScopeKey)`；`currentScopeKey = computed(() => chat.currentSessionId ? 'session:'+id : 'new')`。
2. `@input` 触发 300ms 防抖 `saveDraft(currentScopeKey, inputText)`；页面 `onBeforeUnmount` flush 一次保存。
3. 切换会话：先 `saveDraft(oldScope, inputText)` → `chat.selectSession(newId)` → `inputText = loadDraft(newScope)`。
4. 发送：
   - `chat.isStreaming` 为 true 时禁用（`sendMessage` 亦会 early return，见 `stores/chat.ts:707`），该 early return 不清草稿；
   - 离线 → 不调用 `chat.sendMessage()`，提示"当前离线，内容已保存为草稿"，保留草稿；
   - 在线 → 记录 `oldScopeKey`，调用 `chat.sendMessage()`；等 `isStreaming` 归 false 后检查最后一条 assistant 消息的 `error`：
     - 无 error：`clearDraft(oldScopeKey)` 且 `clearDraft(currentScopeKey)`；
     - 有 error：保留草稿，错误已在消息上展示。
5. 单测覆盖：防抖保存、切换会话保存/加载、流式 early return 不清草稿、按 `error` 判定成功/失败并清理两个 scope。

- [ ] **Step 1: 实现草稿防抖存取与会话切换生命周期**
- [ ] **Step 2: 实现基于 assistant.error 的发送成功判定与双 scope 清理**
- [ ] **Step 3: 补充生命周期单测并运行**

---

## Task 4: E2E 与回归验证

**Files:**
- Modify: `frontend/e2e/mobile-offline-sync.spec.ts`（追加用例）或新建 `frontend/e2e/mobile-chat-draft.spec.ts`

**Details:**
1. E2E：断网 → 输入长文本 → 切后台/重载 → 草稿恢复 → 恢复网络 → 发送成功 → 草稿清空（含 `new` 与 `session:<id>` 两个 scope）→ 会话列表出现新消息。
2. 验证桌面 `/chat` 不受影响：typecheck 与桌面构建通过。
3. 回归命令：`npm test`、`npm run typecheck`、`npm run build:mobile`、`npm run build`。

- [ ] **Step 1: 编写草稿恢复与发送清理的 E2E 用例**
- [ ] **Step 2: 全量单测、桌面与移动端构建通过**

---

## Verification Checklist

- [ ] `chat_drafts` 不进入 `sync_queue`，登出后随本地库清空
- [ ] 草稿跨重载保留；切换会话旧草稿保存、新草稿加载
- [ ] 流式期间不重复发送且不清草稿
- [ ] 发送成功按 `assistant.error` 判定并清理 `new` + `session:<id>` 两个 scope
- [ ] 离线不发假回复、不自动补发草稿
- [ ] 桌面 `/chat` 与 `Chat.vue` 未改动
- [ ] `npm test` / `npm run typecheck` / `npm run build:mobile` / `npm run build` 全绿
