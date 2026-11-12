-- V022: AI Agent Skills (Anthropic 3-level progressive disclosure standard)
CREATE TABLE IF NOT EXISTS ai_skills (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(64) NOT NULL,
    description VARCHAR(1024) NOT NULL,
    license VARCHAR(64),
    compatibility VARCHAR(500),
    category VARCHAR(64) NOT NULL DEFAULT 'finance',
    tags TEXT,
    enabled SMALLINT NOT NULL DEFAULT 1,
    instructions_l2 TEXT NOT NULL,
    backend VARCHAR(32) NOT NULL DEFAULT 'local' CHECK (backend IN ('local', 'remote')),
    exec_root TEXT,
    exec_path TEXT,
    exec_args TEXT,
    exec_env TEXT,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    max_output_bytes INTEGER NOT NULL DEFAULT 65536,
    is_builtin SMALLINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, name)
);

CREATE INDEX IF NOT EXISTS idx_ai_skills_user_enabled ON ai_skills(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_ai_skills_category ON ai_skills(user_id, category);
