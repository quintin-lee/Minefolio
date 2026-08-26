# AI 功能问题修复 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按优先级修复 Minefolio AI 对话助手的正确性、安全与产品缺口，使「财务助手」名副其实且在 SQLite/Postgres 下可用。

**Architecture:** 保持现有三层结构（`ai_controller` → `ai_service` → `ai_session_repo` / `ai_settings_repo`）与前端 `api/ai.ts` + `stores/chat.ts` + `Chat.vue`。先修数据与权限路径，再修流式 UX，最后再单独立项 Tool calling / 财务上下文注入。

**Tech Stack:** C23 + csilk AI/SSE、SQLite/PostgreSQL、Vue 3 + Pinia、fetch ReadableStream

**依据:** 代码审查（非运行时实测）。关键文件：
- `backend/src/services/ai_service.c`
- `backend/src/controllers/ai_controller.c`
- `backend/src/repositories/ai_session_repo.c`
- `backend/src/repositories/ai_settings_repo.c`
- `backend/src/common/ai_config.c`
- `backend/sql/migration.sql` / `migration_postgres.sql`
- `frontend/src/api/ai.ts` / `stores/chat.ts` / `views/Chat.vue`

---

## 问题总览（按严重度）

### P0 — 正确性 / 安全（必须先修）

| ID | 问题 | 位置 | 影响 |
|----|------|------|------|
| P0-1 | `ai_message_recent` 取的是**最早** N 条，不是最近 N 条 | `ai_session_repo.c`：`ORDER BY created_at ASC LIMIT ?` | 长对话上下文错误，模型看不到近期消息 |
| P0-2 | `regenerate=true` 仍把用户消息再拼进 messages 并再 `INSERT` 一条 user | `ai_service.c` | 重复用户消息、上下文膨胀、历史脏数据 |
| P0-3 | `GET /api/ai/sessions/:id/messages` 不校验 session 归属 | `messages_list_handler` | 跨用户读取他人会话（IDOR） |
| P0-4 | Postgres migration **缺少** `ai_sessions` / `ai_messages` 表 | `migration_postgres.sql` | Postgres 部署 AI 无法建表；`ai_traces.session_id` 悬空引用 |
| P0-5 | AI SQL 大量使用 SQLite `datetime(...)` / `datetime('now')` | session/trace/settings repo | Postgres 下列表/保存失败 |
| P0-6 | `parse_string_array` 分配写错：`sizeof(char*) * n + 1` | `ai_controller.c` / `ai_config.c` | 堆缓冲区溢出（应为 `(n+1)*sizeof(char*)`） |
| P0-7 | API Key 落到 `/tmp/ai_config_db.json` 明文 | `ai_init` | 多用户机器上密钥泄露风险 |

### P1 — 产品承诺落空 / 明显 UX 缺陷

| ID | 问题 | 位置 | 影响 |
|----|------|------|------|
| P1-1 | 无 Tool calling / 无用户财务数据注入；UI 却引导「分析我的收支」 | design 范围外 + `Chat.vue` quick prompts | 助手无法回答用户真实账户数据，只能空谈 |
| P1-2 | 前端先 `createSession`，后端自动标题逻辑要求 `session_id<=0` | `chat.ts` + `ai_chat_handler` | 会话标题永远停在「新对话」 |
| P1-3 | `ai_session_get` 返回查询数组，前端当对象读 `raw.id` | repo + `sendMessage` 刷新 | 标题/更新时间刷新失败 |
| P1-4 | 默认模型用 `prov->models[0]`，忽略 `default_model` | `ai_chat_handler` | 设置页默认模型不生效 |
| P1-5 | 流式时已有空 assistant 气泡 + 额外「思考中」行 | `Chat.vue` | 双气泡、布局抖动 |
| P1-6 | 设计有「停止生成」，实现无 `AbortController` / 无停止按钮 | `ai.ts` / `Chat.vue` | 无法中断长回复 |
| P1-7 | Markdown `v-html` 未消毒 | `renderMarkdown` | XSS（模型输出恶意 HTML） |
| P1-8 | 「清空记录」只清本地 `messages`，不删库 | `handleClearChat` | 刷新后消息回来，误导用户 |
| P1-9 | 移动端无 Chat 路由/页面 | `views-mobile` / `router/mobile.ts` | 移动端无 AI |

### P2 — 健壮性 / 运维 / 配置

| ID | 问题 | 位置 | 影响 |
|----|------|------|------|
| P2-1 | Heartbeat 只在 `on_chunk` 内检查 | `ai_service.c` | 首 token 很慢时代理仍可能超时 |
| P2-2 | `done` 事件缺 `usage`；错误信息恒为 `"AI request failed"` | `send_done` / 错误路径 | 与设计不符，难排查 |
| P2-3 | 保存配置时 `ai_shutdown`/`ai_init` 与进行中的 chat 竞态 | `settings_ai_update_handler` | 潜在 UAF / 中断会话 |
| P2-4 | `!g_ai` 直接拒绝，即使请求指定了另一可用 provider | `ai_chat_handler` | 默认 provider 坏则全站 AI 不可用 |
| P2-5 | `ai_config_save` 未 `chmod 0600`；DB `ai_settings` 存明文 key | config / settings | 与设计安全要求不符 |
| P2-6 | Ollama `fetch-models` 走 `/models` + Bearer | `ai_service_fetch_models` | Ollama 更常见 `/api/tags`，拉取失败 |
| P2-7 | 模型 key 用 `provider/model` 再 `split('/')` | `Chat.vue` | 模型名含 `/` 时解析错 |
| P2-8 | Settings 系统提示 UI `maxlength=500`，后端缓冲 2048 | `Settings.vue` | 能力被 UI 砍掉 |
| P2-9 | 请求缺省 `temperature`/`top_p` 经 `db_get_num` 变成 `0` | `ai_chat_handler` | 意外强制 temperature=0 |
| P2-10 | AI 聊天路径几乎无集成测试 | `test_link.sh` 仅插一条 ai_traces | 回归无保障 |
| P2-11 | 配置全局单例，任意登录用户可改 AI 设置 | `settings_ai_*` | 多用户自托管时互相覆盖 |

### P3 — 能力演进（单独立项，不塞进本修复批）

| ID | 方向 | 说明 |
|----|------|------|
| P3-1 | Tool calling：查询资产/持仓/收支/报表 | 真正兑现「财务助手」 |
| P3-2 | 受控上下文注入（摘要快照，非全库 dump） | 无 tool 时的轻量方案 |
| P3-3 | 流式取消与服务端协作 | abort 后停止上游 LLM 请求 |
| P3-4 | 按用户隔离 AI 配置 | 多用户场景 |

---

## 文件变更地图

| 文件 | 职责 |
|------|------|
| `backend/src/repositories/ai_session_repo.c` | recent 窗口、消息列表鉴权辅助、get 返回单对象 |
| `backend/src/services/ai_service.c` | regenerate、默认模型、参数缺省、错误信息、临时文件消除 |
| `backend/src/controllers/ai_controller.c` | messages IDOR、parse_string_array、配置热重载安全 |
| `backend/src/common/ai_config.c` | parse_string_array、chmod、可选内存加载 |
| `backend/src/repositories/ai_settings_repo.c` | Postgres 兼容时间函数 |
| `backend/sql/migration_postgres.sql` | 补齐 sessions/messages |
| `frontend/src/stores/chat.ts` | 标题、session 创建策略、abort |
| `frontend/src/api/ai.ts` | AbortSignal、SSE 解析 |
| `frontend/src/views/Chat.vue` | 双气泡、停止按钮、XSS、清空语义 |
| `backend/tests/test_link.sh` (+ mock) | AI 回归用例 |

---

## Phase A — P0 正确性与安全

### Task A1: 修复上下文窗口（最近 N 条）

**Files:** `backend/src/repositories/ai_session_repo.c`

- [x] **Step 1:** 将 `ai_message_recent` 改为子查询取最近 N 条再按时间正序返回
- [x] **Step 2:** （编译验证；完整断言留待 C4 集成测试）
- [x] **Step 3:** 已实现

### Task A2: 修复 regenerate 重复 user

**Files:** `backend/src/services/ai_service.c`

- [x] **Step 1:** `regenerate` 时不 append/insert 当前 user content
- [x] **Step 2:** 非 regenerate 保持现有路径
- [x] **Step 3:** （留待 C4）
- [x] **Step 4:** 已实现

### Task A3: Messages 列表 IDOR

**Files:** `backend/src/controllers/ai_controller.c`

- [x] **Step 1:** `messages_list_handler` 先校验 session 归属
- [x] **Step 2:** （留待 C4）
- [x] **Step 3:** 已实现

### Task A4: Postgres schema + SQL 方言

**Files:** `migration_postgres.sql`, session/trace/settings repos

- [x] **Step 1:** Postgres migration 补齐 `ai_sessions` / `ai_messages`
- [x] **Step 2:** 去掉 AI 路径上的 `datetime(...)` / `datetime('now')`，改用可移植列名 / `CURRENT_TIMESTAMP`
- [x] **Step 3:** （Postgres 冒烟留待环境验证）
- [x] **Step 4:** 已实现

### Task A5: `parse_string_array` 堆溢出

**Files:** `ai_controller.c`, `ai_config.c`

- [x] **Step 1:** 改为 `malloc(sizeof(char*) * (size_t)(n + 1))`
- [x] **Step 2:** 已实现

### Task A6: 去掉 `/tmp` 明文配置落盘

**Files:** `ai_service.c`, `ai_config.c/.h`

- [x] **Step 1:** 新增 `ai_config_load_json`；`ai_init` 直接从内存加载
- [x] **Step 2:** 已实现

---

## Phase B — P1 UX / 产品一致性

### Task B1: 会话标题与 get 单对象

**Files:** `ai_session_repo.c`, `ai_controller.c`, `stores/chat.ts`

- [ ] **Step 1:** `ai_session_get` 返回数组首元素对象（或新函数 `ai_session_get_one`），`respond_ok` 给前端单对象。
- [ ] **Step 2:** 二选一（推荐前者）：
  - **A:** 前端「新对话」不调 `createSession`，首条消息让后端 `session_id=0` 自动建会话并截断标题；SSE/`done` 或首包带回 `session_id`。
  - **B:** 保留预创建，但首条消息后后端用首句更新 title。
- [ ] **Step 3:** 验证侧边栏标题变为首句摘要。
- [ ] **Step 4:** Commit：`fix(ai): 🐛 session title and get-session shape`

### Task B2: 默认模型生效

**Files:** `ai_service.c`

- [ ] **Step 1:** `model_buf` 优先级：`model_override` → `g_config.default_model`（若属于该 provider）→ `prov->models[0]` → `"default"`。
- [ ] **Step 2:** Commit：`fix(ai): 🐛 honor default_model when chatting`

### Task B3: 流式 UI、停止、XSS、清空

**Files:** `Chat.vue`, `chat.ts`, `ai.ts`

- [ ] **Step 1:** 去掉多余「思考中」行；用最后一条空 assistant + typing indicator CSS。
- [ ] **Step 2:** `chatStream` 支持 `AbortSignal`；UI 增加「停止」；abort 后 `isStreaming=false`。
- [ ] **Step 3:** `marked` 输出经 DOMPurify（或等价）再 `v-html`。
- [ ] **Step 4:** 「清空」改为删除当前 session 消息 API，或改为「删除会话」并文案对齐；禁止只清本地。
- [ ] **Step 5:** Commit：`fix(ai): 🐛 streaming UX stop abort and sanitize markdown`

### Task B4: 快捷提示与能力对齐（短期）

**Files:** `Chat.vue`, 可选 system_prompt 文案

- [ ] **Step 1:** 在接入 Tool 之前，将 quick prompts 改为不依赖「我的账户数据」的通用理财问题，或加提示「当前版本尚未接入账户数据」。
- [ ] **Step 2:** Commit：`fix(ai): 🎨 align empty-state prompts with actual capabilities`

> 移动端 Chat（P1-9）可另开 Phase；本批可不阻塞桌面修复。

---

## Phase C — P2 健壮性与测试

### Task C1: 请求参数缺省与错误可观测性

**Files:** `ai_service.c`

- [ ] **Step 1:** 仅当 JSON 显式含 `temperature`/`top_p`/`max_tokens` 时才写入 `req`（用 `csilk_json_get` 判存在）。
- [ ] **Step 2:** `done` 带上 usage；`error` 透出 `ai_res.error_message`（注意脱敏，勿回传 API key）。
- [ ] **Step 3:** Commit：`fix(ai): 🐛 optional sampling params and richer SSE errors`

### Task C2: Provider 选择与热重载

**Files:** `ai_service.c`, `ai_controller.c`

- [ ] **Step 1:** 请求指定 provider 时可临时 `csilk_ai_new`，不依赖 `g_ai` 必须存在。
- [ ] **Step 2:** 配置更新：用新实例原子替换，或加引用计数/互斥，避免 chat 中途 `ai_free(g_ai)`。
- [ ] **Step 3:** Commit：`fix(ai): 🐛 safer provider init and config reload`

### Task C3: Ollama models、密钥文件权限

**Files:** `ai_service.c`, `ai_config.c`

- [ ] **Step 1:** `id==ollama` 时请求 `{base}/api/tags` 解析 `models[].name`。
- [ ] **Step 2:** `ai_config_save` 后 `chmod(path, 0600)`。
- [ ] **Step 3:** Commit：`fix(ai): 🐛 ollama model fetch and config file mode`

### Task C4: 集成测试

**Files:** `backend/tests/test_link.sh`, `mock_openai_server.js`

- [ ] **Step 1:** 用例：配置 mock provider → chat SSE → DB 有 user+assistant；regenerate 不双写 user；messages IDOR；recent 窗口。
- [ ] **Step 2:** 跑 `./tests/test_link.sh` 全绿。
- [ ] **Step 3:** Commit：`test(ai): ✅ cover chat stream regenerate and authz`

---

## Phase D — 能力演进（另开 design，不在本 plan 实现）

单独写 `docs/superpowers/specs/YYYY-MM-DD-ai-tools-design.md`，再开 plan。建议范围：

1. 只读 tools：`get_net_worth_summary`、`list_assets`、`expense_monthly`、`holdings_pnl`
2. 服务端执行 tool，结果回灌模型；禁止任意 SQL
3. 审计写入 `ai_traces.metadata`
4. 前端展示 tool 调用卡片（可选）

在 design 批准前**不要**在本修复批里半吊子加 tool。

---

## 建议执行顺序

```
A1 → A2 → A5 → A3 → A6 → A4 → B2 → B1 → B3 → B4 → C1 → C2 → C3 → C4
```

每完成一个 Task 保持可编译；A 阶段结束后跑一次后端构建 + 相关手动/脚本验证再进入 B。

---

## 验收标准

- [ ] 超过 `context_size` 条历史时，模型上下文为**最近** N 条
- [ ] Regenerate 不增加额外 user 行
- [ ] 无法用自己的 JWT 读取他人 session messages
- [ ] Postgres migration 可创建 sessions/messages 并完成一轮 chat
- [ ] 无 `/tmp/ai_config_db.json` 落盘
- [ ] 新对话标题来自首句（或明确更新）
- [ ] `default_model` 生效
- [ ] 流式无双气泡；可停止；Markdown XSS 有防护
- [ ] `test_link.sh` 含 AI 相关断言且通过

---

## 非目标（本 plan 明确不做）

- RAG / 知识库
- 语音
- 完整移动端 Chat UI（可跟进）
- Tool calling 完整实现（Phase D 另立项）
