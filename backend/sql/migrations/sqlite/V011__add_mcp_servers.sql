-- V011: MCP 服务器与工具 schema 缓存 (AI 支持外部 MCP 工具源)
CREATE TABLE IF NOT EXISTS mcp_server (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    transport TEXT NOT NULL CHECK (transport IN ('http','stdio')),
    url TEXT,
    command TEXT,
    args TEXT,
    env TEXT,
    headers TEXT,
    secret_ref TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    discovered_at TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, name)
);

CREATE TABLE IF NOT EXISTS mcp_server_tool (
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

CREATE INDEX IF NOT EXISTS idx_mcp_server_user ON mcp_server(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_mcp_server_tool_user ON mcp_server_tool(user_id);
