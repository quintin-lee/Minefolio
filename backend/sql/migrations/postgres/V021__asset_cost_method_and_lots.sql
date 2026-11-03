-- V021: 资产多策略成本结转与交易批次明细表 (PostgreSQL)
ALTER TABLE assets ADD COLUMN IF NOT EXISTS cost_method VARCHAR(32) DEFAULT 'weighted_average';

CREATE TABLE IF NOT EXISTS transaction_lots (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL,
    asset_id            BIGINT NOT NULL,
    buy_tx_id           BIGINT NOT NULL,
    buy_date            VARCHAR(32) NOT NULL,
    original_quantity   VARCHAR(64) NOT NULL,
    remaining_quantity  VARCHAR(64) NOT NULL,
    cost_per_unit       VARCHAR(64) NOT NULL,
    fee_allocated       VARCHAR(64) NOT NULL,
    status              VARCHAR(16) DEFAULT 'open',
    created_at          TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(buy_tx_id) REFERENCES transactions(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_lots_open 
    ON transaction_lots(user_id, asset_id, status, buy_date ASC, id ASC);
