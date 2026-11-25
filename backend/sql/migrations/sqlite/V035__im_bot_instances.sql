-- V035: 用户级独立 iLink 微信机器人实例注册表 (SQLite)
-- 每个 Minefolio 用户一个独立机器人实例；bot_token 永不入库，仅保存在网关 state 目录。

CREATE TABLE IF NOT EXISTS im_bot_instances (
    id                         INTEGER PRIMARY KEY AUTOINCREMENT,
    mf_user_id                 INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    platform                   TEXT NOT NULL DEFAULT 'wechat',
    adapter                    TEXT NOT NULL DEFAULT 'ilink',
    ilink_bot_id               TEXT,
    ilink_user_id              TEXT,
    im_user_id                 TEXT,
    status                     TEXT NOT NULL DEFAULT 'pending_login',
    qr_content                 TEXT,
    qr_expires_at              TEXT,
    login_epoch                INTEGER NOT NULL DEFAULT 0,
    pending_verify_code        TEXT,
    pending_verify_expires_at  TEXT,
    verify_attempts            INTEGER NOT NULL DEFAULT 0,
    enabled                    INTEGER NOT NULL DEFAULT 1,
    last_error                 TEXT,
    last_seen_at               TEXT,
    created_at                 TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at                 TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(mf_user_id, platform, adapter)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_im_user
    ON im_bot_instances(platform, im_user_id) WHERE im_user_id IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_im_instances_platform_bot
    ON im_bot_instances(platform, ilink_bot_id) WHERE ilink_bot_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_im_instances_user ON im_bot_instances(mf_user_id);
