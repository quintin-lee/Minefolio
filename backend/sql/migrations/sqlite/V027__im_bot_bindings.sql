-- V027: IM 机器人绑定关系
-- 将 IM 平台账号 (QQ / 微信个人号) 映射到 Minefolio 用户，作为只读查账机器人的身份锚点。
-- 能力收敛：scope='readonly'，IM 入口只放行只读工具 (非动账)，写操作引导回 App。

CREATE TABLE IF NOT EXISTS im_bot_bindings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    platform     TEXT NOT NULL,                -- 'qq' | 'wechat'
    im_user_id   TEXT NOT NULL,                -- QQ: uid; wechat: wechaty peer/self id
    mf_user_id   INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    scope        TEXT NOT NULL DEFAULT 'readonly',  -- 'readonly' | 'query+confirm' (本期仅 readonly)
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(platform, im_user_id)
);

CREATE INDEX idx_im_bindings_mf ON im_bot_bindings(mf_user_id);
