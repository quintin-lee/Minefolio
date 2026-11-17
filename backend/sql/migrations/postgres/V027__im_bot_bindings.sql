-- V027: IM 机器人绑定关系 (PostgreSQL)
-- 将 IM 平台账号 (QQ / 微信个人号) 映射到 Minefolio 用户，作为只读查账机器人的身份锚点。

CREATE TABLE IF NOT EXISTS im_bot_bindings (
    id           BIGSERIAL PRIMARY KEY,
    platform     TEXT NOT NULL,
    im_user_id   TEXT NOT NULL,
    mf_user_id   BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    scope        TEXT NOT NULL DEFAULT 'readonly',
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   TEXT NOT NULL DEFAULT to_char(now(), 'YYYY-MM-DD HH24:MI:SS'),
    UNIQUE(platform, im_user_id)
);

CREATE INDEX IF NOT EXISTS idx_im_bindings_mf ON im_bot_bindings(mf_user_id);
