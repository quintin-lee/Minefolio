-- V012: 转账表外键级联与账本关联支持
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
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO transfers_new (id, user_id, from_asset_id, to_asset_id, amount, currency, transfer_date, note, ledger_id, created_at)
SELECT t.id, t.user_id, t.from_asset_id, t.to_asset_id, t.amount, t.currency, t.transfer_date, t.note,
       (SELECT a.ledger_id FROM assets a WHERE a.id = t.from_asset_id),
       t.created_at
FROM transfers t;

DROP TABLE IF EXISTS transfers;
ALTER TABLE transfers_new RENAME TO transfers;

CREATE INDEX IF NOT EXISTS idx_transfers_user ON transfers(user_id);
CREATE INDEX IF NOT EXISTS idx_transfers_ledger ON transfers(ledger_id);
CREATE INDEX IF NOT EXISTS idx_transfers_from_asset ON transfers(from_asset_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_asset ON transfers(to_asset_id);
