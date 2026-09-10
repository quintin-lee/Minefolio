-- V008: 回填业务数据账本归属并将分类播种状态提升到账本级 (PostgreSQL)
UPDATE categories c
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = c.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE c.ledger_id IS NULL;

UPDATE assets a
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = a.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE a.ledger_id IS NULL;

UPDATE transactions t
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = t.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE t.ledger_id IS NULL;

UPDATE daily_expenses de
SET ledger_id = (SELECT l.id FROM ledgers l
                 WHERE l.owner_id = de.user_id AND l.is_default = 1
                 ORDER BY l.id LIMIT 1)
WHERE de.ledger_id IS NULL;

DROP TABLE IF EXISTS category_seed_state;

CREATE TABLE category_seed_state (
    user_id    BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id  BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    seeded_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, ledger_id)
);

INSERT INTO category_seed_state (user_id, ledger_id)
SELECT user_id, ledger_id
FROM categories
WHERE ledger_id IS NOT NULL
GROUP BY user_id, ledger_id
ON CONFLICT (user_id, ledger_id) DO NOTHING;
