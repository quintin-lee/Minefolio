-- V001: 用户与鉴权基础体系 (PostgreSQL)
CREATE TABLE IF NOT EXISTS users (
    id                BIGSERIAL PRIMARY KEY,
    username          TEXT UNIQUE NOT NULL,
    password          TEXT NOT NULL,
    token_version     BIGINT NOT NULL DEFAULT 0,
    totp_secret       TEXT DEFAULT '',
    totp_enabled      BOOLEAN DEFAULT FALSE,
    totp_backup_codes TEXT DEFAULT '',
    oauth_provider    TEXT DEFAULT '',
    oauth_id          TEXT DEFAULT '',
    created_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS category_seed_state (
    user_id    BIGINT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    seeded_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
