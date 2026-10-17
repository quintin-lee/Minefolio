-- V014: 转账表显式双向关联 transactions 外键与历史数据回填
CREATE TABLE IF NOT EXISTS transfers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     INTEGER REFERENCES ledgers(id) ON DELETE SET NULL,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfers_new (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     INTEGER REFERENCES ledgers(id) ON DELETE SET NULL,
    from_tx_id    INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    to_tx_id      INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 回填已有历史转账记录的外键关联
INSERT INTO transfers_new (id, user_id, from_asset_id, to_asset_id, amount, currency, transfer_date, note, ledger_id, from_tx_id, to_tx_id, created_at)
SELECT t.id, t.user_id, t.from_asset_id, t.to_asset_id, t.amount, t.currency, t.transfer_date, t.note, t.ledger_id,
       (SELECT tx.id FROM transactions tx
        WHERE tx.user_id = t.user_id
          AND tx.transaction_type = 'transfer_out'
          AND tx.asset_id = t.from_asset_id
          AND tx.linked_asset_id = t.to_asset_id
          AND ABS(tx.amount - t.amount) < 0.000001
        ORDER BY tx.id DESC LIMIT 1),
       (SELECT tx.id FROM transactions tx
        WHERE tx.user_id = t.user_id
          AND tx.transaction_type = 'transfer_in'
          AND tx.asset_id = t.to_asset_id
          AND tx.linked_asset_id = t.from_asset_id
          AND ABS(tx.amount - t.amount) < 0.000001
        ORDER BY tx.id DESC LIMIT 1),
       t.created_at
FROM transfers t;

DROP TABLE IF EXISTS transfers;
ALTER TABLE transfers_new RENAME TO transfers;

CREATE INDEX IF NOT EXISTS idx_transfers_user ON transfers(user_id);
CREATE INDEX IF NOT EXISTS idx_transfers_ledger ON transfers(ledger_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_asset ON transfers(from_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_asset ON transfers(to_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
