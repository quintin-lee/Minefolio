-- V017: 账户维度 — accounts 表 + assets.account_id + 存量回填「默认账户」 (PostgreSQL)
CREATE TABLE IF NOT EXISTS accounts (
    id           BIGSERIAL PRIMARY KEY,
    user_id      BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ledger_id    BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    name         TEXT NOT NULL,
    institution  VARCHAR(32) NOT NULL DEFAULT 'other'
                 CHECK (institution IN ('bank_card','platform','broker','fund_platform','cash_wallet','other')),
    account_no   TEXT,
    currency     VARCHAR(16) DEFAULT 'CNY',
    icon         VARCHAR(64),
    note         TEXT,
    sort_order   INTEGER NOT NULL DEFAULT 0,
    archived     BOOLEAN NOT NULL DEFAULT false,
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, ledger_id, name)
);

CREATE INDEX IF NOT EXISTS idx_accounts_user ON accounts(user_id);
CREATE INDEX IF NOT EXISTS idx_accounts_ledger ON accounts(ledger_id);

-- 资产挂账户（纯容器：账户不记余额；RESTRICT 为账户删除的第二道闸）
ALTER TABLE assets ADD COLUMN IF NOT EXISTS account_id BIGINT REFERENCES accounts(id) ON DELETE RESTRICT;
CREATE INDEX IF NOT EXISTS idx_assets_account ON assets(account_id);

-- 存量回填：每个 (user, ledger) 自动建「默认账户」收纳存量资产。
-- GROUP BY 解析后的 ledger_id 表达式，避免同一 (user, ledger) 因原始 NULL/非 NULL 分组
-- 不同而在单条 INSERT...SELECT 物化阶段撞 UNIQUE(user_id, ledger_id, name)。
WITH grouped AS (
    SELECT
        assets.user_id AS user_id,
        COALESCE(
            assets.ledger_id,
            (SELECT l.id FROM ledgers l
             WHERE l.owner_id = assets.user_id AND l.is_default = 1
             ORDER BY l.id LIMIT 1)
        ) AS ledger_id
    FROM assets
    GROUP BY
        assets.user_id,
        COALESCE(
            assets.ledger_id,
            (SELECT l.id FROM ledgers l
             WHERE l.owner_id = assets.user_id AND l.is_default = 1
             ORDER BY l.id LIMIT 1)
        )
)
INSERT INTO accounts (user_id, ledger_id, name, institution, currency, icon, sort_order)
SELECT
    g.user_id,
    g.ledger_id,
    '默认账户',
    'cash_wallet',
    'CNY',
    'ph:wallet',
    0
FROM grouped g
WHERE g.ledger_id IS NOT NULL
  AND NOT EXISTS (
    SELECT 1 FROM accounts x
    WHERE x.user_id = g.user_id AND x.ledger_id = g.ledger_id AND x.name = '默认账户'
  );

UPDATE assets
SET account_id = (
    SELECT ac.id FROM accounts ac
    WHERE ac.user_id = assets.user_id
      AND ac.ledger_id = COALESCE(
            assets.ledger_id,
            (SELECT l.id FROM ledgers l
             WHERE l.owner_id = assets.user_id AND l.is_default = 1
             ORDER BY l.id LIMIT 1)
        )
      AND ac.name = '默认账户'
    ORDER BY ac.id
    LIMIT 1
)
WHERE account_id IS NULL;
