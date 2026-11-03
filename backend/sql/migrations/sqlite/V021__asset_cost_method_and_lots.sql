-- V021: 资产多策略成本结转与交易批次明细表
ALTER TABLE assets ADD COLUMN cost_method TEXT DEFAULT 'weighted_average';

CREATE TABLE IF NOT EXISTS transaction_lots (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL,
    asset_id            INTEGER NOT NULL,
    buy_tx_id           INTEGER NOT NULL,
    buy_date            TEXT NOT NULL,
    original_quantity   TEXT NOT NULL,
    remaining_quantity  TEXT NOT NULL,
    cost_per_unit       TEXT NOT NULL,
    fee_allocated       TEXT NOT NULL,
    status              TEXT DEFAULT 'open',
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(buy_tx_id) REFERENCES transactions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_lots_open 
    ON transaction_lots(user_id, asset_id, status, buy_date ASC, id ASC);
