-- V031: 账户级与字段级 RBAC 细粒度权限控制表 (SQLite)

CREATE TABLE IF NOT EXISTS account_permissions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    ledger_id        INTEGER NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    account_id       INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    permission_level TEXT NOT NULL CHECK(permission_level IN ('none', 'viewer', 'operator', 'manager')),
    mask_balance     INTEGER NOT NULL DEFAULT 0,
    mask_account_no  INTEGER NOT NULL DEFAULT 0,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (ledger_id, account_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_account_perm_user_ledger 
    ON account_permissions(user_id, ledger_id);

CREATE INDEX IF NOT EXISTS idx_account_perm_account 
    ON account_permissions(account_id, user_id);
