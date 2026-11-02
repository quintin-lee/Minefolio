-- V020: 账本检查点快照与交易生效汇率 (PostgreSQL)
CREATE TABLE IF NOT EXISTS ledger_snapshots (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL,
    asset_id            BIGINT NOT NULL,
    snapshot_date       VARCHAR(32) NOT NULL,
    quantity            VARCHAR(64) NOT NULL,
    cost_basis          VARCHAR(64) NOT NULL,
    net_value           VARCHAR(64) NOT NULL,
    current_value       VARCHAR(64) NOT NULL,
    realized_pnl        VARCHAR(64) NOT NULL,
    balance             VARCHAR(64) NOT NULL,
    tx_checkpoint_date  VARCHAR(32) NOT NULL,
    tx_checkpoint_id    BIGINT NOT NULL,
    lots_snapshot_json  TEXT,
    created_at          TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, asset_id, snapshot_date)
);

CREATE INDEX IF NOT EXISTS idx_snapshots_lookup 
    ON ledger_snapshots(user_id, asset_id, snapshot_date DESC);

ALTER TABLE transactions ADD COLUMN IF NOT EXISTS fx_rate_to_cny DOUBLE PRECISION DEFAULT 1.0;

UPDATE transactions SET fx_rate_to_cny = 1.0 
WHERE asset_id IN (SELECT id FROM assets WHERE currency = 'CNY');
