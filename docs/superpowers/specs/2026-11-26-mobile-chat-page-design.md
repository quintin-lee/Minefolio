# 移动端专用 AI 聊天页设计（子项目 C）

**项目**：Minefolio 移动端功能完善  
**子项目**：C — 移动端专用 AI 聊天页 + 本地草稿缓存  
**日期**：2026-11-26  
**状态**：设计待批准 → 进入 spec-review

---

## 1. 目标

把当前"桌面聊天页在窄屏下裁剪"的体验升级为专用移动端页面：

1. 新增 `views-mobile/ChatMobile.vue`，`/m/chat` 改为指向该页，桌面 `/chat` 与原 `Chat.vue` 不受影响。
2. 复用 `useChatStore` 的全部会话/消息/流式/工作流能力，保持与桌面端一致的服务端数据。
3. 加入**本地草稿持久化**：断网、切后台、误关页面后输入内容不丢失，联网后可选择继续发送。

---

## 2. 现状回顾

| 能力 | 现状 | 依据 |
|------|------|------|
| 路由 | `/m/chat` 直接复用桌面 `Chat.vue` | `router/mobile.ts:36` |
| 移动端适配 | `Chat.vue` 有抽屉、窄屏布局、`isMobileMode()` 路径切换、SSE 同域凭证修复 | `Chat.vue:576,645,668,795` |
| 会话/消息状态 | `useChatStore` 提供 sessions/messages/isStreaming/sendMessage 等 | `stores/chat.ts:61,63,67,706` |
| 输入状态 | `inputText` 是 `Chat.vue` 的局部 ref，无持久化 | `Chat.vue:493,777` |
| 草稿 | 仓库中无聊天草稿机制（仅消息编辑态 `editDraft`） | `Chat.vue:331` |
| 本地库 | D 已建立 sql.js 本地库；目前无聊天相关表 | `db/schema.ts` |

**结论**：会话与消息数据层可完全复用；缺的是专用移动 UI 与输入草稿的本地持久化。

---

## 3. 架构设计

### 3.1 页面与路由

- 新建 `frontend/src/views-mobile/ChatMobile.vue`。
- `router/mobile.ts:36` 的 `component` 从 `@/views/Chat.vue` 改为 `@/views-mobile/ChatMobile.vue`，路由名 `/m/chat` 保持不变，底部 Tab 与 Dashboard 快捷入口无需改动。

### 3.2 复用边界

| 复用项 | 来源 | 说明 |
|--------|------|------|
| 会话/消息/发送/流式 | `useChatStore` (`stores/chat.ts`) | 不复制状态逻辑，直接 `chat.sendMessage()` |
| 消息渲染 | `ChatMessageContent.vue` | 保持与桌面一致的富文本/工具卡片/金融实体渲染 |
| 快捷提示 | `PromptStarters.vue` | 空会话时展示 |
| 工作流入口 | `WorkflowBar.vue`、`WorkflowSlashMenu.vue` | **不在本子项目范围内**；工作流触发/参数面板留待 E 子项目评估，避免"最小可用集"边界模糊 |
| 草稿存取 | 新增 `utils/chat-draft.ts` | 见 3.3 |

**不复用**：`Chat.vue` 的桌面侧边栏布局、多面板并排、分析面板宽屏形态。移动端用抽屉 + 单列消息流 + 底部输入栏。

### 3.3 本地草稿持久化

**新增本地表**（`db/schema.ts` 的 `LOCAL_SCHEMA`）：

```sql
CREATE TABLE IF NOT EXISTS chat_drafts (
  scope_key  TEXT PRIMARY KEY,
  content    TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
```

- `scope_key`：`session:<id>`（已有会话）或 `new`（尚未创建会话的新对话）。
- 该表为移动端专用，不参与 `SyncEngine` 同步、不入 `sync_queue`、不上传服务端。
- 与 D 的本地库一致，经 `persist()` 加密写入 localStorage，App 重启后仍在。

**工具模块** `frontend/src/utils/chat-draft.ts`：

```ts
export function loadDraft(scopeKey: string): string
export function saveDraft(scopeKey: string, content: string): void   // upsert + persist
export function clearDraft(scopeKey: string): void
```

- `loadDraft`：读 `content`，无记录返回空串。
- `saveDraft`：`INSERT ... ON CONFLICT(scope_key) DO UPDATE SET content=?, updated_at=?`，写入后 `persist()`。
- `clearDraft`：`DELETE FROM chat_drafts WHERE scope_key=?` + `persist()`。

### 3.4 输入与草稿生命周期

1. `ChatMobile.vue` 维护 `inputText` ref，初值 `loadDraft(currentScopeKey)`。
2. `@input` 触发 300ms 防抖 `saveDraft(scopeKey, inputText)`。
3. 切换会话时：先保存当前 scope 草稿 → 更新 `currentScopeKey` → 重新 `loadDraft`。
4. 点击发送：
   - `chat.isStreaming === true` 时**禁用发送按钮**；`sendMessage()` 在该状态下也会直接 return（`stores/chat.ts:707`），该 early return **不清草稿**；
   - 离线 → 不调用 `chat.sendMessage()`，提示"当前离线，内容已保存为草稿"，保留草稿；
   - 在线 → 记录发送前的 `oldScopeKey` 后调用 `chat.sendMessage()`。`doSendMessage` 内部可能 `createNewSession()`（`stores/chat.ts:722`），使 `currentSessionId` 从无到有，即 scope_key 从 `new` 变为 `session:<id>`；
   - **成功判定不能依赖 `await` 是否抛错**：`doSendMessage` 把流式异常写入最后一条 assistant 消息的 `error` 字段并吞掉（`stores/chat.ts:798-804`），`sendMessage` 不 reject（`stores/chat.ts:706-717`）。需等 `isStreaming` 归 false 后检查该消息的 `error`：为空才判定成功，非空则保留草稿并在消息上展示错误；
   - 清理范围：同时 `clearDraft(oldScopeKey)` 与 `clearDraft(currentScopeKey)`，避免 `new` scope 遗留孤儿草稿。
5. 页面卸载前 flush 一次 `saveDraft`，避免防抖窗口内丢失最后输入。

### 3.5 离线行为边界

- AI 聊天依赖服务端推理，**不实现"离线假回复"**。离线时只允许：浏览已缓存会话、编辑草稿、切换模型设置（若已缓存）。
- 网络恢复时不自动发送草稿；由用户点击发送，避免未经确认的付费/审计敏感请求。
- 草稿表不进入同步队列，不存在跨账号泄露路径；登出时 `resetLocalDb()` 已清空整库，草稿随之清除。

### 3.6 移动端 UI 结构

```
<mobile-chat>
  <header> 会话标题 / 新建 / 会话列表按钮 </header>
  <session-drawer v-if="showSessions"> 会话列表（chat.sessions） </session-drawer>
  <message-list> ChatMessageContent 逐条渲染；isStreaming 时显示流式占位 </message-list>
  <input-bar>
    textarea v-model="inputText"
    发送按钮（离线时禁用并显示草稿提示）
    工作流斜杠菜单（复用 WorkflowSlashMenu）
  </input-bar>
</mobile-chat>
```

- 空会话时展示 `PromptStarters` 与"历史会话"入口。
- 消息列表滚动策略沿用 `Chat.vue` 的"只看最后一条消息 ID"规则（见 AGENTS.md 第 12 条），不监听 `messages.length`。

---

## 4. 接口约定

### 4.1 `utils/chat-draft.ts`

| 函数 | 签名 | 行为 |
|------|------|------|
| `loadDraft` | `(scopeKey: string) => string` | 读本地表，无记录返回 `''` |
| `saveDraft` | `(scopeKey: string, content: string) => void` | upsert + `persist()` |
| `clearDraft` | `(scopeKey: string) => void` | delete + `persist()` |

### 4.2 `ChatMobile.vue` 状态

- `inputText: ref<string>` — 与草稿同步的输入内容。
- `currentScopeKey: computed<string>` — `chat.currentSessionId ? 'session:'+id : 'new'`。
- `isOffline: ref<boolean>` — 由 `navigator.onLine` / `online`/`offline` 事件维护，仅用于 UI 提示，不触发同步。
- `showSessions: ref<boolean>` — 会话抽屉开关。

---

## 5. 错误处理

| 场景 | 处理 |
|------|------|
| 本地库未初始化 | `initLocalDb()` 由 `main-mobile.ts bootstrap` 保证；工具函数在 `getDb()` 抛错时向上传播，页面显示错误提示 |
| 草稿写入失败（配额不足） | 由 D 的 `minefolio:localdb-quota-exceeded` 事件统一提示；不阻断输入，只在下次保存重试 |
| 离线发送 | 不调用后端，保留草稿，提示"已保存为草稿" |
| 发送失败/流式中断 | 保留草稿，`chat.abortCurrentStream()` 后允许重试 |
| 切换账号/登出 | `resetLocalDb()` 清空本地库，草稿与会话一并清除 |

---

## 6. 测试计划

### 6.1 前端单元测试（vitest）

- `saveDraft` / `loadDraft` / `clearDraft` 的基本读写与 upsert；
- 模拟重载本地库后草稿仍在（D 的 `db.local.spec.ts` 同样的 reload 模式）；
- 切换会话时先保存旧 scope、再加载新 scope；
- 流式期间发送按钮禁用，`sendMessage` 的 early return 不清草稿；
- 离线发送不调用 `chat.sendMessage()` 且草稿保留；
- 发送后按 assistant 消息的 `error` 判定：无 error 时同时清理 `new` 与 `session:<id>` 两个 scope 的草稿，有 error 时保留草稿。

### 6.2 E2E（Playwright + 请求拦截）

断开网络 → 输入长文本 → 切后台/重载 → 草稿恢复 → 恢复网络 → 发送成功 → 草稿清空 → 会话列表中出现新消息。

### 6.3 回归

`npm test`、`npm run typecheck`、`npm run build:mobile` 全绿；桌面 `/chat` 不受影响（路由隔离）。

---

## 7. 实施顺序

1. **阶段一**：`db/schema.ts` 加 `chat_drafts` 表，新增 `utils/chat-draft.ts`，补单元测试。
2. **阶段二**：新建 `ChatMobile.vue`（复用 store 与消息组件，接入草稿生命周期），改 `/m/chat` 路由。
3. **阶段三**：E2E 与全量回归，逐项提交。

---

## 8. 与其他子项目的关系

- **A/B（写入口补全）**：独立，可并行。本子项目不依赖其完成。
- **D（离线优先）**：复用 D 的本地库与 `persist()`，但不进入 `sync_queue`，不产生冲突项。
- **E（移动端特有能力）**：草稿表是移动端专属能力；后续可在此基础上扩展"离线快捷指令模板"等。
