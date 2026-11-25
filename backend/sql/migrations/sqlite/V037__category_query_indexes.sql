-- V037: 为分类按账本/类型查询与播种状态检查补充复合索引 (SQLite)
CREATE INDEX IF NOT EXISTS idx_categories_ledger_sort
    ON categories(ledger_id, sort_order, name);
CREATE INDEX IF NOT EXISTS idx_categories_ledger_type_sort
    ON categories(ledger_id, type, sort_order, name);
CREATE INDEX IF NOT EXISTS idx_category_seed_state_ledger
    ON category_seed_state(ledger_id);
