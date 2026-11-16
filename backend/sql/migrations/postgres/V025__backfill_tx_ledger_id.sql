-- V025: 历史交易记录关联账本回填 (PostgreSQL)
-- 将交易记录中缺少账本关联 (ledger_id 为 NULL) 的历史记录回填为资产所属账本或用户默认账本。

UPDATE transactions t
SET ledger_id = COALESCE(
    (SELECT a.ledger_id FROM assets a WHERE a.id = t.asset_id),
    (SELECT l.id FROM ledgers l WHERE l.owner_id = t.user_id AND l.is_default = 1 LIMIT 1)
)
WHERE t.ledger_id IS NULL;
