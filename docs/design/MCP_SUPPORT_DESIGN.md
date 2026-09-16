# Minefolio MCP 支持 — 设计文档 v1.0

**状态**：待评审
**作者**：Sisyphus
**日期**：2026-09-15
**适用范围**：`backend/src/services/ai/tools/`、`backend/src/services/ai/runtime/`、`backend/sql/migrations/{sqlite,postgres}/V011__*`、`frontend/src/components/settings/`、`frontend/src/api/`

---

## 0. 设计目标与范围

- **目标**：让 Minefolio AI 的 LLM 工具调用循环可以**同时看到并使用**两类工具：
  1. 内置 7 类财务工具（asset/transaction/expense/transfer/cashflow/portfolio/report，约 30 个）
  2. 用户配置的外部 MCP 服务器上暴露的任意工具（如 GitHub、Notion、Slack、自建脚本）
- **非目标（本轮不做）**：Minefolio 作为 MCP *服务器*（路径 B），保持单向集成，控制复杂度。
- **协议范围**：MCP 2025-06-18 规范，仅支持 `streamable-HTTP` 与 `stdio` 两种 transport。旧版 `SSE`（2024-11）不实现——已被官方标记 deprecated，streamable-HTTP 是完整超集。
- **预算与策略**：MCP 工具**计入**现有 `tool_budget` / `cost_budget` / `timeout_ms` / `max_iterations`，不另开预算池。
- **向后兼容**：内置 registry 静态数组 + `pthread_once` 不变；MCP 工具在**用户会话开始时**动态生成 `csilk_ai_tool_t` 并追加到 `ctx->tools[]`，dispatcher 按 `mcp:<serverId>:` 前缀路由到远端通道。零改动内置工具的执行链路。

---

## 1. 总体架构

```
┌────────────────────────────────────────────────────────────────────┐
│                          AI Runtime (loop.c)                       │
│  csilk_ai_chat 携带 tools[] ──► LLM 输出 tool_calls ──► 路由      │
└────────────────────────┬───────────────────────────────────────────┘
                         │ tool name
                         ▼
        ┌──────────────────────────────────────────────┐
        │ dispatcher.c / ai_tools_execute_parsed 路由  │
        │  name 匹配 mcp:<serverId>:<tool> 前缀？       │
        │   ├─ 否 → 现有 8 步内置流水线（不变）          │
        │   └─ 是 → MCP 远端流水线（§3）                │
        └──────────────┬───────────────────────────────┘
                       │
        ┌──────────────┴───────────────────────────────┐
        │ MCP 子层（新增）services/ai/tools/mcp/       │
        │  ├── mcp_client.h/.c      JSON-RPC 2.0 帧    │
        │  ├── mcp_config.h/.c      DB 读写、凭证、启停│
        │  ├── mcp_bridge.h/.c      远端 tool → csilk  │
        │  └── mcp_discover.c       tools/list 缓存     │
        └──────────────────────────────────────────────┘
```

**关键决策 D1**：不复制内置 registry。MCP 工具通过 bridge 在**用户发起 AI 会话时**动态生成 `csilk_ai_tool_t` 追加到 `ctx->tools[]`，与内置工具同列传给 LLM。
**关键决策 D2**：MCP 工具 schema 用 LRU 缓存（按 user_id+server_id+tool_name 三元组），TTL 30 分钟；避免每次 AI 会话都拉 `tools/list`。
**关键决策 D3**：stdio transport 由后端 fork 子进程并维持长连接池；http transport 走 csilk 现有的 HTTP client。

---

## 2. 数据模型（迁移 V011，sqlite + postgres 双方言）

### 2.1 表 `mcp_server`

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | BIGINT/INTEGER | PK | sqlite 用 `INTEGER PRIMARY KEY AUTOINCREMENT`；PG 用 `BIGSERIAL` |
| `user_id` | BIGINT/INTEGER | NOT NULL, FK → users(id) ON DELETE CASCADE | 归属用户 |
| `name` | TEXT/VARCHAR(128) | NOT NULL | 用户侧展示名，**唯一键 (user_id, name)** |
| `transport` | TEXT/VARCHAR(16) | NOT NULL, CHECK IN ('http','stdio') | 传输层 |
| `url` | TEXT/VARCHAR(512) | http 必填 | 完整 URL，含 scheme，末尾不带 `/` |
| `command` | TEXT/VARCHAR(512) | stdio 必填 | 可执行文件绝对路径或 PATH 解析名 |
| `args` | TEXT/JSONB | stdio | JSON 数组字符串，如 `["--verbose"]` |
| `env` | TEXT/JSONB | stdio | JSON 对象字符串，继承宿主 env 并 override |
| `headers` | TEXT/JSONB | http | JSON 对象，可含 `Authorization`、`X-Api-Key` |
| `secret_ref` | TEXT/VARCHAR(128) | NULL | 非空时实际凭证存 `secret` 表（§4 安全），此处是 ref 名 |
| `enabled` | BOOLEAN | NOT NULL, DEFAULT true | 停用后工具不进入 LLM 工具集 |
| `timeout_ms` | INT | NOT NULL, DEFAULT 30000 | 单次 `tools/call` 超时 |
| `discovered_at` | TIMESTAMP | NULL | 最近一次 `tools/list` 成功时间 |
| `created_at` / `updated_at` | TIMESTAMP | NOT NULL | 审计 |

**唯一约束**：`UNIQUE (user_id, name)`——同用户下 MCP 服务器名不重复。

### 2.2 表 `mcp_server_tool`（schema 缓存）

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | BIGINT/INTEGER | PK | |
| `server_id` | BIGINT/INTEGER | NOT NULL, FK → mcp_server(id) ON DELETE CASCADE | 归属服务器 |
| `user_id` | BIGINT/INTEGER | NOT NULL, FK → users(id) | 冗余字段避免 join |
| `tool_name` | TEXT/VARCHAR(128) | NOT NULL | 远端原始 tool 名 |
| `qualified_name` | TEXT/VARCHAR(192) | NOT NULL, **UNIQUE (user_id, qualified_name)** | `mcp:<serverId>:<toolName>`，LLM 看到的名字 |
| `description` | TEXT | NULL | 远端 `inputSchema.description` |
| `input_schema` | TEXT/JSONB | NOT NULL | 远端 `inputSchema` 原文 |
| `is_mutation` | BOOLEAN | NOT NULL, DEFAULT false | 从 description/注解推断；默认 false |
| `risk_level` | TEXT/VARCHAR(16) | NOT NULL, DEFAULT 'medium' | `low`/`medium`/`high`，见 §5 策略 |
| `fetched_at` | TIMESTAMP | NOT NULL | 拉取时间 |

**设计选择**：`qualified_name` 列直接存前缀化名字，让 LLM 与 dispatcher 共享同一事实源；`UNIQUE` 约束保证前缀化命名无冲突。

### 2.3 迁移脚本

`backend/sql/migrations/sqlite/V011__add_mcp_servers.sql`：
```sql
CREATE TABLE mcp_server (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    transport TEXT NOT NULL CHECK (transport IN ('http','stdio')),
    url TEXT, command TEXT, args TEXT, env TEXT, headers TEXT,
    secret_ref TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    discovered_at TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, name)
);
CREATE TABLE mcp_server_tool (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL REFERENCES mcp_server(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL,
    tool_name TEXT NOT NULL,
    qualified_name TEXT NOT NULL,
    description TEXT,
    input_schema TEXT NOT NULL,
    is_mutation INTEGER NOT NULL DEFAULT 0,
    risk_level TEXT NOT NULL DEFAULT 'medium' CHECK (risk_level IN ('low','medium','high')),
    fetched_at TEXT NOT NULL,
    UNIQUE (user_id, qualified_name)
);
CREATE INDEX idx_mcp_server_user ON mcp_server(user_id, enabled);
CREATE INDEX idx_mcp_server_tool_user ON mcp_server_tool(user_id);
```

`backend/sql/migrations/postgres/V011__add_mcp_servers.sql`：
```sql
CREATE TABLE mcp_server (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(128) NOT NULL,
    transport VARCHAR(16) NOT NULL CHECK (transport IN ('http','stdio')),
    url VARCHAR(512), command VARCHAR(512),
    args JSONB, env JSONB, headers JSONB,
    secret_ref VARCHAR(128),
    enabled BOOLEAN NOT NULL DEFAULT true,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    discovered_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (user_id, name)
);
CREATE TABLE mcp_server_tool (
    id BIGSERIAL PRIMARY KEY,
    server_id BIGINT NOT NULL REFERENCES mcp_server(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL,
    tool_name VARCHAR(128) NOT NULL,
    qualified_name VARCHAR(192) NOT NULL,
    description TEXT,
    input_schema JSONB NOT NULL,
    is_mutation BOOLEAN NOT NULL DEFAULT false,
    risk_level VARCHAR(16) NOT NULL DEFAULT 'medium' CHECK (risk_level IN ('low','medium','high')),
    fetched_at TIMESTAMPTZ NOT NULL,
    UNIQUE (user_id, qualified_name)
);
CREATE INDEX idx_mcp_server_user ON mcp_server(user_id, enabled);
CREATE INDEX idx_mcp_server_tool_user ON mcp_server_tool(user_id);
```

**迁移引擎**：自动发现 → 版本排序 → SHA-256 CRLF 校验 → 事务应用。V011 不依赖 V010 的新列，可独立执行。

---

## 3. 模块划分与接口

### 3.1 `services/ai/tools/mcp/mcp_config.h`（CRUD 仓库层）

```c
typedef enum { MCP_TRANSPORT_HTTP = 0, MCP_TRANSPORT_STDIO = 1 } mcp_transport_t;

typedef struct {
    int64_t   id;
    int64_t   user_id;
    char      name[128];
    mcp_transport_t transport;
    char      url[512];
    char      command[512];
    char      args_json[1024];      // 原样 JSON 数组
    char      env_json[2048];
    char      headers_json[2048];
    char      secret_ref[128];
    bool      enabled;
    int       timeout_ms;
} mcp_server_t;

int       mcp_server_list(csilk_db_pool_t* pool, int64_t user_id,
                          mcp_server_t** out, size_t* count);
int       mcp_server_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id,
                         mcp_server_t* out);
int64_t   mcp_server_insert(csilk_db_pool_t* pool, int64_t user_id,
                            const mcp_server_t* s);
int       mcp_server_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id,
                            const mcp_server_t* s);
int       mcp_server_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/* 工具 schema 缓存 */
typedef struct {
    int64_t  server_id;
    char     tool_name[128];
    char     qualified_name[192];
    char     input_schema[4096];
    char     description[512];
    bool     is_mutation;
} mcp_server_tool_t;

int       mcp_server_tools_load(csilk_db_pool_t* pool, int64_t user_id,
                                int64_t server_id,
                                mcp_server_tool_t** out, size_t* count);
int       mcp_server_tools_upsert(csilk_db_pool_t* pool, int64_t user_id,
                                  int64_t server_id,
                                  const mcp_server_tool_t* tools, size_t count);
```

### 3.2 `services/ai/tools/mcp/mcp_client.h`（JSON-RPC 2.0 帧收发）

```c
typedef enum {
    MCP_OK = 0,
    MCP_ERR_PROTOCOL,      // JSON-RPC 帧格式错误
    MCP_ERR_TRANSPORT,      // 连接失败 / 超时
    MCP_ERR_AUTH,          // 401/403
    MCP_ERR_REMOTE,        // 远端业务错误（result.isError）
    MCP_ERR_CANCELLED,
    MCP_ERR_TIMEOUT,
} mcp_status_t;

typedef struct mcp_server_t mcp_server_t;

typedef struct {
    int64_t              server_id;
    char                 session_id[64];   // MCP 协议层 session（http transport）
    char                 process_argv[8][256]; // stdio transport
    int                  argv_count;
    /* http */
    char*                base_url;
    csilk_json_t*        default_headers;
    /* stdio */
    int                  stdin_pipe, stdout_pipe;
} mcp_session_t;

mcp_status_t mcp_initialize(mcp_session_t* s, const mcp_server_t* cfg,
                            const char* client_name, const char* client_version);
mcp_status_t mcp_tools_list(mcp_session_t* s, int64_t user_id,
                            mcp_server_tool_t** out, size_t* count);
mcp_status_t mcp_tools_call(mcp_session_t* s, int64_t user_id,
                            const char* tool_name, const csilk_json_t* arguments,
                            char** out_result_json, int* out_is_error);
void         mcp_session_close(mcp_session_t* s);
```

**stdio 实现要点**：
- 用 `posix_spawn` 启动子进程，继承宿主 env + `cfg->env_json` 覆盖。
- stdin/stdout 各开一个 pipe；stderr 重定向到 `/dev/null`（避免子进程日志污染）。
- JSON-RPC 消息按换行分隔（MCP 规范 2025-06 的 stdio 编码：每行一个 JSON 对象，**没有** LSP 风格 Content-Length 头）。
- 子进程退出时 `SIGCHLD` 回收，会话标记不可用。

**http 实现要点**：
- `initialize` 发 `POST {url}/initialize`，响应头 `Mcp-Session-Id` 必须保存后续请求。
- `tools/list`、`tools/call` 发 `POST {url}/`，body 是 JSON-RPC envelope，响应可能是 `application/json`（单次）或 `text/event-stream`（SSE 流）；本设计**只接受 JSON 单帧**，SSE 返回视为 `MCP_ERR_PROTOCOL`（MCP 规范 2025-06 的 streamable HTTP 允许服务端选择，但客户端可只要 JSON）。
- 每个请求带 `Mcp-Session-Id` 头；401/403 → `MCP_ERR_AUTH`；非 2xx → `MCP_ERR_REMOTE`。

### 3.3 `services/ai/tools/mcp/mcp_bridge.h`（远端 tool → csilk_ai_tool_t 适配）

```c
/* 把用户启用的 MCP 服务器工具加载为 LLM 可见工具集，追加到内置工具之后 */
int mcp_bridge_load_for_user(csilk_db_pool_t* pool,
                             int64_t user_id,
                             const csilk_ai_tool_t* built_in, size_t built_in_count,
                             csilk_ai_tool_t** out_tools, size_t* out_count);

/* dispatcher 路由：前缀命中 → 远端执行 */
char* mcp_bridge_dispatch(const ai_tool_context_t* ctx,
                          const char* qualified_name,
                          const csilk_json_t* args);
```

**`mcp_bridge_load_for_user` 行为**：
1. 调 `mcp_server_list(pool, user_id, &servers, &n)`。
2. 对每个 `enabled=true` 的服务器，调 `mcp_server_tools_load`；**缓存 30 分钟有效直接命中**，否则调 `mcp_tools_list` 拉取并 upsert。
3. 把每个 `mcp_server_tool_t` 包装成 `csilk_ai_tool_t{ .type="function", .function.name=qualified_name, .function.description=description, .function.parameters_json=input_schema }`。
4. 输出 `built_in + mcp_tools` 的合并数组。
5. 内存：`out_tools` 为堆分配，调用方 `csilk_ai_tool_array_free`。

**`mcp_bridge_dispatch` 行为**（供 dispatcher 调用）：
1. 解析 `qualified_name` 拆出 `server_id` 与远端 `tool_name`。
2. 查 `mcp_server_get`，若 `enabled=false` → 返回 `{"error":"mcp_server_disabled"}`。
3. 查 `mcp_server_tools_load`，找到该 tool；未找到 → 触发一次 `mcp_tools_list` 重拉，仍无 → `{"error":"mcp_tool_unknown"}`。
4. 按 `risk_level` 走策略（§5）。
5. 按 `transport` 选择 `http` 或 `stdio` session（stdio 走连接池，http 走短连接）。
6. 调 `mcp_tools_call`，把远端 `result.content` 原样回传给 LLM（MCP 规范：`result.content` 是 `content[]` 数组，每项有 `type` 与 `text`/`data`）。
7. 把 `result.isError` 透传到返回 JSON 的 `error` 字段。

### 3.4 Dispatcher 路由改造（`services/ai/tools/dispatcher.c`）

现状 `ai_tool_dispatch_parsed` 走 8 步内置流水线。改造：

```c
char*
ai_tool_dispatch_parsed(const ai_tool_context_t* ctx,
                        const char*              tool_name,
                        const csilk_json_t*      args)
{
    /* 新增：MCP 前缀命中 → 远端通道 */
    if (tool_name && strncmp(tool_name, "mcp:", 4) == 0) {
        return mcp_bridge_dispatch(ctx, tool_name, args);
    }
    /* 原有 8 步内置流水线（不变） */
    ...
}
```

**审计**：MCP 分支也复用 `ai_audit_log`，`tool` 字段写 qualified_name，`result_summary` 标注 `remote`。

### 3.5 Runtime loop 集成（`services/ai/runtime/loop.c`）

改动 `ai_runtime_execute_stream` 第 4 步（工具集解析）：

```c
/* 4. 解析可用工具集（内置 + 当前用户启用的 MCP 工具） */
size_t tool_count = 0;
const csilk_ai_tool_t* built_in = ai_tools_get_definitions(&tool_count);
size_t mcp_count = 0;
csilk_ai_tool_t* merged = NULL;
if (mcp_bridge_load_for_user(pool, ctx->user_id, built_in, tool_count,
                             &merged, &mcp_count) == 0) {
    tools = merged; tool_count = mcp_count;
}
/* 会话结束时：free(merged) —— 新增 §10 清理分支 */
```

**关键点**：`ctx->tools[]` 原本只传内置，现在传 merged。LLM 看到的工具名里包含 `mcp:<serverId>:<tool>`，自然引导它按 qualified_name 调用；dispatcher 按前缀路由。

**预算不变**：`tool_budget`、`cost_budget` 仍是全局值，MCP 调用与内置调用同样计 1。

### 3.6 REST API（`controllers/ai_mcp_controller.c` + `application/mcp/`）

| 方法 | 路径 | 行为 | 响应 |
|---|---|---|---|
| GET | `/api/ai/mcp/servers` | 当前用户所有 MCP 服务器 | `{code:0, data:[{id,name,transport,enabled,timeout_ms,discovered_at,tool_count}]}` |
| POST | `/api/ai/mcp/servers` | 创建 | `{code:0, data:{id}}` |
| PUT | `/api/ai/mcp/servers/:id` | 更新 | `{code:0, data:{}}` |
| DELETE | `/api/ai/mcp/servers/:id` | 删除（级联删 tools） | `{code:0, data:{}}` |
| POST | `/api/ai/mcp/servers/:id/test` | 探活：initialize + tools/list；回写 `discovered_at` | `{code:0, data:{status:"ok", tools:[...]}}` 或 `{code:1002, message:"<error>"}` |
| GET | `/api/ai/mcp/servers/:id/tools` | 读缓存（不重拉） | `{code:0, data:[{tool_name, qualified_name, description, is_mutation, risk_level}]}` |
| POST | `/api/ai/mcp/servers/:id/refresh` | 强制重拉 tools/list | `{code:0, data:{tool_count:N}}` |

**参数白名单校验**：
- `name`：`^[a-z0-9_-]{1,64}$`
- `url`：`^https?://` 且 host 非 `localhost`（除非 `MINEFOLIO_MCP_ALLOW_LOCAL=1`，否则视为 SSRF 防护）
- `command`：长度 ≤ 512，禁止 `;`、`&`、`|`、反引号（防 shell 注入；stdio 直接 `execvp`，不走 shell）
- `args/env/headers`：JSON 序列化前用 `db_get_num/db_get_int` 校验，长度 ≤ 8192
- `timeout_ms`：`1000 ≤ x ≤ 120000`

**鉴权**：走现有 JWT middleware（`middlewares/jwt.c`），所有端点要求 `Authorization: Bearer`；`CSRF` 中间件对写操作生效。

**错误码**（对齐 §6 错误分类）：
- `1001` 未认证
- `1002` 参数校验失败
- `1003` 未找到（`id` 不属于当前 `user_id`）
- `1004` 冲突（`name` 重复）

---

## 4. 安全设计

### 4.1 凭证管理

- **stdio 的 `env` 与 http 的 `headers` 含敏感键**（如 `GITHUB_TOKEN`、`Authorization`）：
  - REST 写入时，后端把 `secret_ref` 设成 `<random 16 hex>`，实际值写 `secret` 表（`config/secret.h` 的 Secret Provider）。
  - 读 API 永远不回显原值；`mcp_server_t` 的 `headers_json`/`env_json` 在 DB 中只存 `secret_ref`，运行时合并。
  - 删除 `mcp_server` 时级联删 `secret`。
- **不信任远端 MCP 服务器**：远端返回的 `inputSchema` 直接进 LLM 工具描述。在 `mcp_bridge_load_for_user` 里做最小净化：
  - 丢弃 schema 里任何 `$ref` 指向非 `#` 开头（避免 SSRF 解析外部 schema）。
  - 字符串字段 `maxLength` 上限 8192；数组 `maxItems` 上限 100。
  - description 截断到 1024 字符（防 LLM 提示注入放大）。

### 4.2 SSRF 防护（http transport）

- 配置保存时校验 host 非内网保留段（`10/8`、`172.16/12`、`192.168/16`、`127/8`、`::1`、`169.254/16`）除非 `MINEFOLIO_MCP_ALLOW_LOCAL=1`。
- 运行时 connect 前再 resolve 一次 DNS（防 DNS rebinding），命中保留段则拒绝。
- 超时 30s（默认），不跟随 302 到内网。

### 4.3 远端工具风险分级（§5）

- 默认 `medium`。
- 远端 tool 的 `description` 命中关键词（`delete`/`remove`/`drop`/`send`/`publish`/`post`/`transfer`/`pay`）→ 升级 `high`。
- 命中 `read`/`list`/`get`/`query`/`search`/`describe` → 降级 `low`。
- 用户可在 settings UI 手动覆盖（存 `mcp_server_tool.risk_level`）。

---

## 5. 策略引擎集成（`services/ai/policy/`）

MCP 工具复用 `ai_policy_evaluate`，扩展一条新分支：

```
policy.c 新增规则 R-MCP：
IF tool_name starts_with("mcp:")
THEN
  lookup mcp_server_tool by qualified_name
  risk = tool.risk_level   /* low|medium|high */
  IF risk == "high" AND user.perm_level < AI_PERM_WRITE THEN DENY
  IF risk == "high" THEN CONFIRM (requires_confirmation=true, 走 §6 草案流程)
  IF rate_limit(user, tool, 60s) > 30 THEN DENY("mcp_rate_limited")
  ELSE ALLOW
```

- **限频**：每用户每 qualified_name 每分钟 30 次（可调，env `MINEFOLIO_MCP_RATE_PER_MIN`）。
- **确认**：`high` 风险写操作走现有 `ai_policy_confirmation` 草案流程（`confirm_<draftId>` 工具），与内置 `propose_*` 同构。

---

## 6. 错误分类与 LLM 可见反馈

dispatcher 返回给 LLM 的 JSON 字符串统一 schema：

```json
{ "ok": true|false, "result": ..., "error": "..." }
```

| 来源 | 错误码 | 返回 `error` 字段 | LLM 行为引导 |
|---|---|---|---|
| 前缀未命中 | `mcp_server_unknown` | `{"error":"mcp_server_unknown"}` | LLM 放弃该调用 |
| 服务器被禁用 | `mcp_server_disabled` | `{"error":"mcp_server_disabled"}` | 同上 |
| schema 校验失败 | `mcp_schema_invalid` | `{"error":"mcp_schema_invalid","detail":"<msg>"}` | LLM 修正参数重试（≤1 次） |
| 连接失败 | `mcp_transport_down` | `{"error":"mcp_transport_down","detail":"<msg>"}` | LLM 告知用户"远端不可用" |
| 鉴权失败 | `mcp_auth_failed` | `{"error":"mcp_auth_failed"}` | LLM 提示用户更新凭证 |
| 远端业务错 | `mcp_remote_error` | `{"error":"mcp_remote_error","detail":"<remote message>"}` | LLM 转述给用户 |
| 超时 | `mcp_timeout` | `{"error":"mcp_timeout","detail":"exceeded 30000ms"}` | 同上 |
| 限频 | `mcp_rate_limited` | `{"error":"mcp_rate_limited"}` | LLM 停止重试 |

**trace**：`ai_trace_add_tool_span` 增加 `source` 字段（`built_in` / `mcp`），前端 trace 视图可区分。

**审计**：所有 MCP 调用（含失败）写 `ai_audit_log`，`stage` 字段：
- 成功执行 → `AI_AUDIT_STAGE_EXECUTION`
- 远端 `isError=true` → `AI_AUDIT_STAGE_EXECUTION`（`success=false`）
- 策略拒绝 → `AI_AUDIT_STAGE_REJECTION`
- 确认草案 → `AI_AUDIT_STAGE_PROPOSAL` / `AI_AUDIT_STAGE_CONFIRMATION`

---

## 7. 前端设计

### 7.1 `frontend/src/api/ai-mcp.ts`

```ts
import { http } from '@/utils/http'
import type {
  McpServer, McpServerCreateReq, McpServerUpdateReq,
  McpServerTool, McpTestResult,
} from '@/types'

export const listMcpServers = () => http.get<McpServer[]>('/api/ai/mcp/servers')
export const createMcpServer = (b: McpServerCreateReq) =>
  http.post<{ id: number }>('/api/ai/mcp/servers', b)
export const updateMcpServer = (id: number, b: McpServerUpdateReq) =>
  http.put<{ id: number }>(`/api/ai/mcp/servers/${id}`, b)
export const deleteMcpServer = (id: number) =>
  http.delete<{ id: number }>(`/api/ai/mcp/servers/${id}`)
export const testMcpServer = (id: number) =>
  http.post<McpTestResult>(`/api/ai/mcp/servers/${id}/test`)
export const getMcpServerTools = (id: number) =>
  http.get<McpServerTool[]>(`/api/ai/mcp/servers/${id}/tools`)
export const refreshMcpServer = (id: number) =>
  http.post<{ tool_count: number }>(`/api/ai/mcp/servers/${id}/refresh`)
```

### 7.2 `frontend/src/types/index.ts` 追加

```ts
export interface McpServer {
  id: number
  name: string
  transport: 'http' | 'stdio'
  url?: string
  command?: string
  args?: string[]
  enabled: boolean
  timeout_ms: number
  discovered_at?: string
  tool_count: number
}
export interface McpServerCreateReq { /* 同上，除 id/discovered_at/tool_count */ }
export interface McpServerTool {
  tool_name: string
  qualified_name: string
  description?: string
  is_mutation: boolean
  risk_level: 'low' | 'medium' | 'high'
}
export interface McpTestResult {
  status: 'ok' | 'error'
  detail?: string
  tools?: McpServerTool[]
}
```

### 7.3 `frontend/src/components/settings/AiMcpManager.vue`

- **列表卡片**（复用 `AiProviderManager.vue` 的 ProviderItem 布局）：name、transport 标签、enabled 开关、tool 数量、最近探活时间、`测试` / `刷新` / `编辑` / `删除` 按钮。
- **添加/编辑对话框**：
  - transport 二选一（radio）：http 显示 url + headers 输入；stdio 显示 command + args 输入（JSON 数组编辑器，简化为逗号分隔）。
  - `secret_ref` 由后端自动处理，前端不暴露。
  - 提交前前端再跑一遍 §3.6 的正则校验（SSR 兜底）。
- **测试连接**：按钮 → `testMcpServer(id)` → 显示 status + 拉回的 tools 列表；失败时显示 `detail`。
- **工具浏览**：侧边抽屉展示该 server 的 tools（按 `risk_level` 着色：绿/橙/红），只读。
- **删除确认**：`ElMessageBox.confirm`，级联删 secret 提示。

### 7.4 嵌入点

- `Settings.vue`（桌面端）：在 `AiProviderManager` 卡片下方加 `AiMcpManager` 卡片，标题"外部 MCP 工具源"。
- `SettingsMobile.vue`：单独路由页（复用 desktop 组件，加 viewport 适配）。

---

## 8. 测试策略

### 8.1 单测（`backend/tests/unit/`）

新增 2 个 CTest 套件：

| 套件 | 覆盖 | 关键断言 |
|---|---|---|
| `test_mcp_config.c` | CRUD + 级联 + 唯一键 | 重复 name → 1004；删 server → tools 级联删；secret 级联删 |
| `test_mcp_client.c` | JSON-RPC 帧解析 + 超时 + SSE 拒绝 + stdio 换行编码 | 非法帧 → `MCP_ERR_PROTOCOL`；SSE 响应 → 拒；stdio 子进程 kill → `MCP_ERR_TRANSPORT` |

mock：本地 `mcp_mock_server.py`（python，json-rpc 标准库）+ `mcp_mock_stdio.py`。

### 8.2 集成（`backend/tests/test_mcp.sh`）

```
启动 mock http server (127.0.0.1:9911) + mock stdio binary (shim)
1. 注册 user
2. POST /api/ai/mcp/servers 创建 2 台服务器（http + stdio 各一）
3. POST /:id/test 探活成功
4. 触发 AI 对话命中一个远端工具（LLM 返回 mcp:1:echo）
5. 断言 ai_audit_log 有 remote span；mcp_server_tool 缓存命中
6. 删 user → 级联删 server + tools + secret
```

断言 12+ 项，走现有 `assert_json` 工具。

### 8.3 回归保护

- 现有 `test_ai_tool_call.sh`（15 case）必须全绿：dispatcher 新增 `mcp:` 前缀分支不影响内置工具。
- 现有 `test_link.sh`（38 case）中"balance 联动"等敏感路径不受 MCP 影响，作 smoke。
- 新增 `test_mcp_budget.sh`（3 case）：`tool_budget=1` 时第 2 个 MCP 调用被拒；`cost_budget` 命中超时；`max_iterations` 命中。

### 8.4 手动冒烟

```
./scripts/dev.sh
# 1. Settings → AI MCP → 添加 → 填 Ollama 本地 MCP 服务器 → 测试 OK
# 2. 对话："用 github 工具读一下 X 仓的 README"
# 3. 看 trace 视图出现 source=mcp 的 span
```

---

## 9. 部署与运维

- **环境变量**：
  - `MINEFOLIO_MCP_ALLOW_LOCAL=0`（默认；允许 127.0.0.1 等内网段）
  - `MINEFOLIO_MCP_RATE_PER_MIN=30`（每用户每工具限频）
  - `MINEFOLIO_MCP_CACHE_TTL=1800`（schema 缓存 30 min）
  - `MINEFOLIO_MCP_STDIO_MAX_PROCS=16`（stdio 连接池上限）
- **Dockerfile**：无需额外依赖（stdio 走 `execvp`，http 走 csilk 自带 client）。
- **docker-compose**：默认 `MINEFOLIO_MCP_ALLOW_LOCAL=1`（容器内访问宿主局域网 MCP 服务器需要）。
- **密钥轮换**：secret_ref 走 `config/secret.c` 的 provider，支持 env 注入或文件挂载。

---

## 10. 资源生命周期与清理

| 对象 | 分配点 | 释放点 | 备注 |
|---|---|---|---|
| `merged tools[]` | `loop.c` §3.5 | `loop.c` 清理（新增 `if (merged) free(merged)`） | 堆分配，每个元素 `csilk_ai_tool_t` 含指针字段需逐字段 free |
| `mcp_session_t` | `mcp_bridge_dispatch` | `mcp_session_close` | http 关连接；stdio 发 `SIGTERM` + `waitpid` |
| `stdio 子进程` | `mcp_initialize` | 连接池回收 / 用户删 server / 后端退出 | `atexit` 兜底 `SIGKILL` |
| `secret` 行 | `mcp_server_insert` | `mcp_server_delete` 级联 | DB FK `ON DELETE CASCADE` |
| `audit_log` 行 | `ai_audit_log` | 按月分区归档（运维脚本） | 不受本设计影响 |

**崩溃恢复**：stdio 子进程残留时，`mcp_initialize` 启动前先 `pkill -f "<command>"`（同用户同 command）；http session 无状态，401 即重 initialize。

---

## 11. 边界条件清单

- **B1**：用户 0 台启用 MCP 服务器 → `mcp_bridge_load_for_user` 返回 0 个 MCP 工具，merged = 内置集，零开销。
- **B2**：远端 `tools/list` 返回 0 个工具 → 缓存写空，LLM 看不到该 server；测试连接报 "server online, 0 tools"。
- **B3**：远端 `tools/list` 返回 > 64 个工具 → 拒绝该 server（`MCP_ERR_PROTOCOL`），用户需远端侧裁剪。
- **B4**：`qualified_name` 长度 > 192 → 拒绝该 tool，日志警告。
- **B5**：stdio 子进程输出非 UTF-8 字节 → `mcp_tools_call` 按 binary 透传，LLM 侧由 csilk 处理。
- **B6**：远端 tool 的 `inputSchema` 缺 `type` 字段 → 补默认 `{ "type": "object", "properties": {}, "additionalProperties": true }`。
- **B7**：`Mcp-Session-Id` 头缺失（远端未实现）→ 客户端兼容：不校验该头。
- **B8**：会话取消（`cancel_token` 置位）→ dispatcher 在 MCP 调用前检查，命中则 `MCP_ERR_CANCELLED`，stdio 子进程 `SIGINT`。
- **B9**：`secret` 表凭证过期（如 GitHub token 失效）→ 首次 401 触发一次 secret 轮换提示（audit 记 `mcp_auth_failed`）。
- **B10**：`max_iterations=1` 且首个 tool_call 是 MCP → 走完整执行，结果回 LLM 后不再迭代（现有行为不变）。

---

## 12. 里程碑与工期

| 里程碑 | 交付 | 工期 | 验证 |
|---|---|---|---|
| M1 | DB 迁移 V011 + `mcp_config.c` + REST CRUD + 前端 `AiMcpManager.vue`（不含探活） | 3 天 | `test_mcp_config.c` 全绿；UI 可增删改查 |
| M2 | `mcp_client.c`（http + stdio）+ `mcp_bridge.c` + 探活 + 前端测试/刷新按钮 | 4 天 | `test_mcp_client.c` + `test_mcp.sh` 全绿 |
| M3 | dispatcher 前缀路由 + policy R-MCP + 限频 + trace source=mcp + 前端风险着色 | 2 天 | `test_mcp_budget.sh` + 现有 `test_ai_tool_call.sh` 回归 |
| M4 | 文档（用户手册 + 运维手册）+ 回归全量 + 发版 | 1 天 | 全测试套件绿 |

**总工期 10 天**（单人，含测试）。M4 前任何一天可中止不影响 M1–M3 已交付价值。

---

## 13. 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| stdio 子进程句柄泄漏 | 中 | 后端 OOM | 连接池 16 上限；空闲 5 min 回收；`atexit` 兜底 |
| 远端 MCP 服务器恶意 schema 注入 LLM | 低 | 提示注入 | §4.1 schema 净化；description 截断 1024 |
| DNS rebinding（http 内网段） | 低 | SSRF | §4.2 运行时二次 resolve |
| 用户配置 100 台 MCP 服务器撑爆 LLM 工具预算 | 中 | LLM 端 400 | 单用户上限 20 台（DB CHECK `tool_count < 20*64`）；LLM 工具总数 > 128 时拒绝该 server 加入 |
| 远端 tool 名称与内置冲突（如远端也叫 `get_assets`） | 中 | LLM 误调 | qualified_name 前缀 `mcp:<id>:` 天然隔离 |
| csilk HTTP client 对 SSE 流解析限制 | 中 | 部分 MCP server 不可用 | 明确 §3.2 只接受 JSON 单帧；SSE 服务端在文档中标注"暂不支持" |

---

## 14. 附录 A：协议帧示例

**initialize（http）**：
```json
POST /mcp/initialize
{ "jsonrpc":"2.0", "id":1, "method":"initialize",
  "params":{"protocolVersion":"2025-06-18",
            "clientInfo":{"name":"minefolio","version":"1.x"},
            "capabilities":{"tools":{}} } }
```
响应头 `Mcp-Session-Id: abc123`，body：
```json
{ "jsonrpc":"2.0","id":1,
  "result":{"protocolVersion":"2025-06-18",
            "serverInfo":{"name":"github-mcp","version":"0.9"},
            "capabilities":{"tools":{"listChanged":true}} } }
```

**tools/list**：
```json
POST /mcp/   (header: Mcp-Session-Id: abc123)
{ "jsonrpc":"2.0","id":2,"method":"tools/list","params":{} }
```
```json
{ "jsonrpc":"2.0","id":2,
  "result":{"tools":[
    {"name":"create_pr","description":"Create a pull request",
     "inputSchema":{"type":"object",
       "properties":{"repo":{"type":"string"},"title":{"type":"string"}},"required":["repo","title"]}}
  ]}}
```

**tools/call**：
```json
POST /mcp/
{ "jsonrpc":"2.0","id":3,"method":"tools/call",
  "params":{"name":"create_pr","arguments":{"repo":"acme/x","title":"fix: y"}} }
```
```json
{ "jsonrpc":"2.0","id":3,
  "result":{"content":[{"type":"text","text":"PR #42 created"}],
            "isError":false} }
```

**stdio**：宿主以换行分隔的 JSON-RPC 帧写入子进程 stdin；每行一帧，MCP 规范 2025-06 的 stdio 编码。

---

## 15. 附录 B：与现有 DDD 分层映射

| 模块 | 层 | 位置 |
|---|---|---|
| `mcp_server` / `mcp_server_tool` | 实体 | `domain/mcp/entity.h` |
| `mcp_server_repo_impl.c` | 仓储 | `infrastructure/repositories/mcp_server_repo_impl.c` |
| `mcp_config.c`（业务规则） | 应用 | `application/mcp/` |
| `mcp_client.c`（协议） | 基础设施 | `services/ai/tools/mcp/` |
| `mcp_bridge.c`（LLM 适配） | 应用 | `services/ai/tools/mcp/` |
| `ai_mcp_controller.c` | 接口 | `interfaces/http/controllers/` |
| `AiMcpManager.vue` + `api/ai-mcp.ts` | 接口（前端） | `frontend/src/components/settings/` |

依赖方向仍严格单向：`interfaces → application → domain`；`services/ai/tools/mcp` 作为 AI 域的"基础设施适配层"，不引入 domain 反向依赖。

---

**评审关注点**（给 Momus / Oracle）：
1. §3.5 `loop.c` 改造：`merged` 数组生命周期是否正确释放（尤其 `csilk_ai_tool_t` 指针字段）。
2. §5 策略 R-MCP 与现有 `ai_policy_evaluate` 的融合点——是新增分支还是替换。
3. §10 stdio 连接池回收与 `atexit` 兜底的实现细节（是否需 `SIGTERM` 后 `SIGKILL` 双段）。
4. §4.1 schema 净化的 `$ref` 白名单是否过严。
5. §8 测试策略：`test_mcp_client.c` 的 mock stdio 子进程在 CI（Linux/macOS）的可移植性。
