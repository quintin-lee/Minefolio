-- V014: 转账表显式双向关联 transactions 外键与历史数据回填
ALTER TABLE transfers ADD COLUMN IF NOT EXISTS from_tx_id BIGINT REFERENCES transactions(id) ON DELETE CASCADE;
ALTER TABLE transfers ADD COLUMN IF NOT EXISTS to_tx_id BIGINT REFERENCES transactions(id) ON DELETE CASCADE;

-- 回填已有历史记录
UPDATE transfers t
SET from_tx_id = (
    SELECT tx.id FROM transactions tx
    WHERE tx.user_id = t.user_id
      AND tx.transaction_type = 'transfer_out'
      AND tx.asset_id = t.from_asset_id
      AND tx.linked_asset_id = t.to_asset_id
      AND ABS(tx.amount - t.amount) < 0.000001
    ORDER BY tx.id DESC LIMIT 1
)
WHERE t.from_tx_id IS NULL;

UPDATE transfers t
SET to_tx_id = (
    SELECT tx.id FROM transactions tx
    WHERE tx.user_id = t.user_id
      AND tx.transaction_type = 'transfer_in'
      AND tx.asset_id = t.to_asset_id
      AND tx.linked_asset_id = t.from_asset_id
      AND ABS(tx.amount - t.amount) < 0.000001
    ORDER BY tx.id DESC LIMIT 1
)
WHERE t.to_tx_id IS NULL;

CREATE INDEX IF NOT EXISTS idx_transfers_from_tx ON transfers(from_tx_id);
CREATE INDEX IF NOT EXISTS idx_transfers_to_tx ON transfers(to_tx_id);
