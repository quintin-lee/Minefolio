-- V040: 补齐 PostgreSQL 转账表索引与 SQLite 对齐 (PostgreSQL)
CREATE INDEX IF NOT EXISTS idx_transfers_user ON transfers(user_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_asset ON transfers(from_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_asset ON transfers(to_asset_id);
