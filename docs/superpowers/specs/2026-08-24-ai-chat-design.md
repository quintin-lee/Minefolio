# AI 对话小助手 — 设计规格

## 概述

在 Minefolio 中增加 AI 对话助手功能，支持多供应商、多模型配置，流式输出。

## 功能范围

- 多供应商配置（OpenAI 兼容 API + Ollama）
- 会话管理（创建、列表、历史、删除）
- SSE 流式对话
- 模型切换（对话级别）
- 系统提示词（ Finance Assistant 角色）

## 不在范围内

- RAG / 知识库检索
- 文件上传给 AI
- 多用户共享会话
- 语音输入/输出

---

## 后端设计

### 配置文件

`config/ai.json`（由 Settings 页面写入）：

```json
{
  "providers": [
    {
      "id": "deepseek",
      "name": "DeepSeek",
      "api_key": "sk-xxx",
      "base_url": "https://api.deepseek.com",
      "models": ["deepseek-chat", "deepseek-reasoner"]
    },
    {
      "id": "ollama",
      "name": "本地 Ollama",
      "api_key": "",
      "base_url": "http://localhost:11434",
      "models": ["qwen2.5:7b", "llama3.2"]
    }
  ],
  "default_provider": "deepseek",
  "default_model": "deepseek-chat",
  "context_size": 20,
  "system_prompt": "你是 Minefolio 财务助手，擅长回答资产管理、投资分析、收支统计相关问题。用中文回答，简洁专业。"
}
```

### 环境变量（启动时覆盖配置）

| 变量 | 说明 |
|---|---|
| `MINEFOLIO_AI_CONFIG` | ai.json 路径，默认 `config/ai.json` |

### 数据库 Schema

```sql
-- sessions 表
CREATE TABLE ai_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    title TEXT NOT NULL DEFAULT '新对话',
    model TEXT NOT NULL DEFAULT 'deepseek-chat',
    provider TEXT NOT NULL DEFAULT 'deepseek',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- messages 表
CREATE TABLE ai_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES ai_sessions(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK(role IN ('user', 'assistant')),
    content TEXT NOT NULL,
    model TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### API 端点

| Method | Path | 说明 |
|---|---|---|
| `GET` | `/api/ai/models` | 返回可用模型列表 |
| `POST` | `/api/ai/chat` | SSE 流式对话 |
| `GET` | `/api/ai/sessions` | 分页列出会话 |
| `POST` | `/api/ai/sessions` | 创建会话 |
| `GET` | `/api/ai/sessions/:id` | 获取会话历史 |
| `PUT` | `/api/ai/sessions/:id` | 更新会话标题/模型 |
| `DELETE` | `/api/ai/sessions/:id` | 删除会话 |
| `GET` | `/api/settings/ai` | 读取 AI 配置（脱敏） |
| `PUT` | `/api/settings/ai` | 更新 AI 配置 |

### SSE 响应格式

```
event: delta
data: {"content": "你好"}

event: done
data: {"finish_reason": "stop", "usage": {"prompt_tokens": 12, "completion_tokens": 30}}

event: error
data: {"message": "rate limited", "code": 429}
```

### 核心流程

```
POST /api/ai/chat
  ↓
1. JWT 校验，获取 user_id
2. 读取 body: {session_id?, content, model?}
3. 加载 session 历史（最近 context_size 条）
4. 构建 messages: [system_prompt, ...history, {role:"user", content}]
5. csilk_sse_init(c)
6. csilk_ai_chat(ai_instance, &req, &res) — 带 on_chunk 回调
   - on_chunk: csilk_sse_send(c, "delta", json)
   - on_error: csilk_sse_send(c, "error", json)
   - on_done:  csilk_sse_send(c, "done", json_usage)
7. 持久化 user message + assistant message 到 db
8. csilk_sse_close(c)
```

### C 代码结构

```
backend/src/
  controllers/ai_controller.c/.h      # 路由注册
  services/ai_service.c/.h            # 对话业务逻辑
  repositories/ai_session_repo.c/.h   # 会话/消息 CRUD
  common/ai_config.c/.h               # ai.json 读写
```

---

## 前端设计

### 新增文件

```
frontend/src/
  views/Chat.vue                      # 对话主页面
  api/ai.ts                           # API 封装
  stores/chat.ts                      # Pinia store（会话+消息）
```

### 路由

```typescript
// router/index.ts
{ path: 'chat', name: 'Chat', component: () => import('@/views/Chat.vue') }
```

### 页面布局

```
┌─────────────────────────────────────────────────────┐
│  🤖 AI助手          [+ 新对话]  [模型选择▼]        │
├───────────────┬─────────────────────────────────────┤
│  会话列表      │                                     │
│               │  ┌─消息区────────────────────────┐  │
│ • 💬 今天理财 │  │ 👤 我的资产分布是怎样的？     │  │
│ • 📊 上月支出 │  │ 🤖 根据当前数据...           │  │
│ • 🔥 基金收益 │  │    （流式逐字渲染）           │  │
│               │  └──────────────────────────────┘  │
│ [+ 新建]       │  ┌────────────────────────────┐   │
│               │  │ 💬 继续问...          [发送] │   │
└───────────────┴─────────────────────────────────┘
```

### 交互行为

- **发送消息**：`fetch('/api/ai/chat', { method: 'POST', body: JSON.stringify({session_id, content, model}) })`
- **流式渲染**：`response.body.getReader()` → 解析 SSE 事件 → 逐字追加
- **停止生成**：`controller.abort()`
- **新对话**：创建空 session，清空消息区
- **切换模型**：从 `/api/ai/models` 获取列表，改变后新消息用新模型
- **会话切换**：加载历史消息到消息区

### chat store 结构

```typescript
// stores/chat.ts
interface ChatState {
  sessions: AiSession[]
  currentSessionId: number | null
  messages: AiMessage[]
  isStreaming: boolean
  currentProvider: string
  availableModels: AiModelOption[]
}

async function sendMessage(content: string)
async function createSession()
async function loadSession(id: number)
async function switchModel(provider: string, model: string)
```

### API 封装

```typescript
// api/ai.ts
export async function chatStream(params: {
  session_id?: number
  content: string
  model?: string
}):  AsyncIterable<{content: string}>

export async function listSessions(): Promise<AiSession[]>
export async function createSession(model?: string): Promise<AiSession>
export async function getMessages(sessionId: number): Promise<AiMessage[]>
export async function deleteSession(id: number): Promise<void>
export async function getModels(): Promise<AiModelOption[]>
```

---

## Settings 页面扩展

在 Settings 页增加 **AI 配置** 区域：

- 供应商列表（可添加/删除）
- 每个供应商：名称、API Key（输入框）、Base URL、可用模型
- 默认供应商和默认模型选择
- 系统提示词编辑
- 上下文窗口大小

配置通过 `PUT /api/settings/ai` 保存。

---

## 安全

- API Key 存储在 `config/ai.json`，文件权限 0600
- GET /api/settings/ai 不返回 API Key（仅返回元数据）
- JWT 保护所有 `/api/ai/*` 端点
- rate limit 与现有 middleware 共用

---

## 文件变更清单

### 后端（新增）
- `backend/src/controllers/ai_controller.c/.h`
- `backend/src/services/ai_service.c/.h`
- `backend/src/repositories/ai_session_repo.c/.h`
- `backend/src/common/ai_config.c/.h`
- `backend/sql/migration.sql`（新增 2 张表）
- `backend/src/main.c`（注册路由 + AI 初始化）

### 前端（新增）
- `frontend/src/views/Chat.vue`
- `frontend/src/api/ai.ts`
- `frontend/src/stores/chat.ts`
- `frontend/src/router/index.ts`（注册路由）
- `frontend/src/views/Layout.vue`（侧边栏菜单增加"AI助手"）
- `frontend/src/views/Settings.vue`（AI 配置区域）
