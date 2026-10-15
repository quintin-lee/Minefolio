-- V012: 转账表外键级联与账本关联支持
CREATE TABLE IF NOT EXISTS transfers (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

ALTER TABLE transfers ADD COLUMN IF NOT EXISTS ledger_id BIGINT REFERENCES ledgers(id) ON DELETE SET NULL;

UPDATE transfers t
SET ledger_id = (SELECT a.ledger_id FROM assets a WHERE a.id = t.from_asset_id)
WHERE t.ledger_id IS NULL;

ALTER TABLE transfers DROP CONSTRAINT IF EXISTS transfers_from_asset_id_fkey;
ALTER TABLE transfers DROP CONSTRAINT IF EXISTS transfers_to_asset_id_fkey;

ALTER TABLE transfers
    ADD CONSTRAINT transfers_from_asset_id_fkey
    FOREIGN KEY (from_asset_id) REFERENCES assets(id) ON DELETE CASCADE;

ALTER TABLE transfers
    ADD CONSTRAINT transfers_to_asset_id_fkey
    FOREIGN KEY (to_asset_id) REFERENCES assets(id) ON DELETE CASCADE;

CREATE INDEX IF NOT EXISTS idx_transfers_ledger ON transfers(ledger_id);
