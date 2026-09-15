-- V010: AI 会话摘要持久化 (长对话滑动窗口 + LLM 摘要) (PostgreSQL)
CREATE TABLE IF NOT EXISTS ai_session_summaries (
    id BIGSERIAL PRIMARY KEY,
    session_id BIGINT NOT NULL UNIQUE REFERENCES ai_sessions(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    summary_text TEXT NOT NULL DEFAULT '',
    token_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_session_summaries_user ON ai_session_summaries(user_id, updated_at);
