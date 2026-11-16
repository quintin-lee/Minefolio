-- V025: 历史交易记录关联账本回填 (SQLite)
-- 将交易记录中缺少账本关联 (ledger_id 为 NULL) 的历史记录回填为资产所属账本或用户默认账本。

UPDATE transactions
SET ledger_id = COALESCE(
    (SELECT a.ledger_id FROM assets a WHERE a.id = transactions.asset_id),
    (SELECT l.id FROM ledgers l WHERE l.owner_id = transactions.user_id AND l.is_default = 1 LIMIT 1)
)
WHERE ledger_id IS NULL;
