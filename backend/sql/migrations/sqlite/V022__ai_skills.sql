-- V022: AI Agent Skills (Anthropic 3-level progressive disclosure standard)
CREATE TABLE IF NOT EXISTS ai_skills (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    license TEXT,
    compatibility TEXT,
    category TEXT NOT NULL DEFAULT 'finance',
    tags TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    instructions_l2 TEXT NOT NULL,
    backend TEXT NOT NULL DEFAULT 'local' CHECK (backend IN ('local', 'remote')),
    exec_root TEXT,
    exec_path TEXT,
    exec_args TEXT,
    exec_env TEXT,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    max_output_bytes INTEGER NOT NULL DEFAULT 65536,
    is_builtin INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, name)
);

CREATE INDEX IF NOT EXISTS idx_ai_skills_user_enabled ON ai_skills(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_ai_skills_category ON ai_skills(user_id, category);
