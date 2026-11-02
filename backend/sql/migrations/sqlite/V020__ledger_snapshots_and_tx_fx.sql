-- V020: 账本检查点快照与交易生效汇率
CREATE TABLE IF NOT EXISTS ledger_snapshots (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL,
    asset_id            INTEGER NOT NULL,
    snapshot_date       TEXT NOT NULL,
    quantity            TEXT NOT NULL,
    cost_basis          TEXT NOT NULL,
    net_value           TEXT NOT NULL,
    current_value       TEXT NOT NULL,
    realized_pnl        TEXT NOT NULL,
    balance             TEXT NOT NULL,
    tx_checkpoint_date  TEXT NOT NULL,
    tx_checkpoint_id    INTEGER NOT NULL,
    lots_snapshot_json  TEXT,
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, asset_id, snapshot_date)
);

CREATE INDEX IF NOT EXISTS idx_snapshots_lookup 
    ON ledger_snapshots(user_id, asset_id, snapshot_date DESC);

-- 为 transactions 添加历史汇率字段
ALTER TABLE transactions ADD COLUMN fx_rate_to_cny REAL DEFAULT 1.0;

-- 本币交易直接置 1.0
UPDATE transactions SET fx_rate_to_cny = 1.0 
WHERE asset_id IN (SELECT id FROM assets WHERE currency = 'CNY');
