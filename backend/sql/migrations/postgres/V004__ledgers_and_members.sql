-- V004: 多账本空间与 RBAC 成员体系 (PostgreSQL)
CREATE TABLE IF NOT EXISTS ledgers (
    id BIGSERIAL PRIMARY KEY,
    owner_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    currency VARCHAR(16) NOT NULL DEFAULT 'CNY',
    icon VARCHAR(64) DEFAULT 'ph:wallet',
    color VARCHAR(32) DEFAULT '#3b82f6',
    is_default INTEGER NOT NULL DEFAULT 0,
    invite_code VARCHAR(32) UNIQUE,
    invite_expires_at TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ledgers_owner ON ledgers(owner_id);
CREATE INDEX IF NOT EXISTS idx_ledgers_invite ON ledgers(invite_code);

CREATE TABLE IF NOT EXISTS ledger_members (
    id BIGSERIAL PRIMARY KEY,
    ledger_id BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role VARCHAR(16) NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')),
    joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (ledger_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_ledger_members_user ON ledger_members(user_id);
CREATE INDEX IF NOT EXISTS idx_ledger_members_ledger ON ledger_members(ledger_id);
