-- V039: iLink 微信对话记账能力开关与短码确认草案 (SQLite)
-- capability 默认 readonly；仅用户主动开启 query+confirm 后，微信对话才允许生成记账草案。
-- im_pending_actions 保存 8 位短码到 draft_token/参数的映射，确认链路不经过 LLM。

ALTER TABLE im_bot_instances
    ADD COLUMN capability TEXT NOT NULL DEFAULT 'readonly';

CREATE TABLE IF NOT EXISTS im_pending_actions (
    code         TEXT PRIMARY KEY,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    session_id   INTEGER NOT NULL,
    action_type  TEXT NOT NULL DEFAULT 'daily_expense',
    draft_token  TEXT NOT NULL,
    args_json    TEXT NOT NULL,
    expires_at   INTEGER NOT NULL,
    consumed_at  INTEGER,
    created_at   INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
);

CREATE INDEX IF NOT EXISTS idx_im_pending_actions_user
    ON im_pending_actions(user_id, expires_at);
