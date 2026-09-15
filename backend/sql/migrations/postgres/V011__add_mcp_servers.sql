-- V011: MCP 服务器与工具 schema 缓存 (AI 支持外部 MCP 工具源) (PostgreSQL)
CREATE TABLE IF NOT EXISTS mcp_server (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(128) NOT NULL,
    transport VARCHAR(16) NOT NULL CHECK (transport IN ('http','stdio')),
    url VARCHAR(512),
    command VARCHAR(512),
    args JSONB,
    env JSONB,
    headers JSONB,
    secret_ref VARCHAR(128),
    enabled BOOLEAN NOT NULL DEFAULT true,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    discovered_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (user_id, name)
);

CREATE TABLE IF NOT EXISTS mcp_server_tool (
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

CREATE INDEX IF NOT EXISTS idx_mcp_server_user ON mcp_server(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_mcp_server_tool_user ON mcp_server_tool(user_id);
