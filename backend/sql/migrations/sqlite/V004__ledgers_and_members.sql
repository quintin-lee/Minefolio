-- V004: 多账本空间与 RBAC 成员体系
CREATE TABLE IF NOT EXISTS ledgers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    currency TEXT NOT NULL DEFAULT 'CNY',
    icon TEXT DEFAULT 'ph:wallet',
    color TEXT DEFAULT '#3b82f6',
    is_default INTEGER NOT NULL DEFAULT 0,
    invite_code TEXT UNIQUE,
    invite_expires_at DATETIME,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ledgers_owner ON ledgers(owner_id);
CREATE INDEX IF NOT EXISTS idx_ledgers_invite ON ledgers(invite_code);

CREATE TABLE IF NOT EXISTS ledger_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ledger_id INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')),
    joined_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (ledger_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_ledger_members_user ON ledger_members(user_id);
CREATE INDEX IF NOT EXISTS idx_ledger_members_ledger ON ledger_members(ledger_id);

-- 业务表 ledger_id 关联
ALTER TABLE assets ADD COLUMN ledger_id INTEGER;
ALTER TABLE transactions ADD COLUMN ledger_id INTEGER;
ALTER TABLE daily_expenses ADD COLUMN ledger_id INTEGER;
ALTER TABLE categories ADD COLUMN ledger_id INTEGER;

CREATE INDEX IF NOT EXISTS idx_assets_ledger ON assets(ledger_id);
CREATE INDEX IF NOT EXISTS idx_transactions_ledger ON transactions(ledger_id);
CREATE INDEX IF NOT EXISTS idx_daily_expenses_ledger ON daily_expenses(ledger_id);
CREATE INDEX IF NOT EXISTS idx_categories_ledger ON categories(ledger_id);
