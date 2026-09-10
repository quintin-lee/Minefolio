-- V008: 回填业务数据账本归属并将分类播种状态提升到账本级
UPDATE categories
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = categories.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE ledger_id IS NULL;

UPDATE assets
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = assets.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE ledger_id IS NULL;

UPDATE transactions
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = transactions.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE ledger_id IS NULL;

UPDATE daily_expenses
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = daily_expenses.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE ledger_id IS NULL;

DROP TABLE IF EXISTS category_seed_state;

CREATE TABLE category_seed_state (
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id  INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    seeded_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, ledger_id)
);

INSERT OR IGNORE INTO category_seed_state (user_id, ledger_id)
SELECT user_id, ledger_id
FROM categories
WHERE ledger_id IS NOT NULL
GROUP BY user_id, ledger_id;
