-- Minefolio PostgreSQL 数据库初始化脚本

CREATE TABLE IF NOT EXISTS users (
    id            BIGSERIAL PRIMARY KEY,
    username      TEXT UNIQUE NOT NULL,
    password      TEXT NOT NULL,
    token_version BIGINT NOT NULL DEFAULT 0,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS categories (
    id         BIGSERIAL PRIMARY KEY,
    user_id    BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    parent_id  BIGINT REFERENCES categories(id) ON DELETE SET NULL,
    type       TEXT NOT NULL DEFAULT 'asset' CHECK(type IN ('asset','income','expense','transaction')),
    asset_type TEXT DEFAULT 'cash' CHECK(asset_type IN (
        'cash','stock','fund','bond','crypto',
        'real_estate','vehicle','other_asset',
        'loan','credit_card','other_liability'
    )),
    currency   TEXT DEFAULT 'CNY',
    icon       TEXT,
    sort_order INTEGER DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, name, parent_id)
);

CREATE TABLE IF NOT EXISTS assets (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   BIGINT NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    name          TEXT NOT NULL,
    account_no    TEXT,
    current_value DECIMAL(18,2) DEFAULT 0,
    quantity      DECIMAL(18,4) NOT NULL DEFAULT 0,
    cost_basis    DECIMAL(18,4) NOT NULL DEFAULT 0,
    net_value     DECIMAL(18,4) NOT NULL DEFAULT 0,
    currency      TEXT DEFAULT 'CNY',
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transactions (
    id               BIGSERIAL PRIMARY KEY,
    user_id          BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset_id         BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    linked_asset_id  BIGINT REFERENCES assets(id) ON DELETE SET NULL,
    category_id      BIGINT REFERENCES categories(id) ON DELETE RESTRICT,
    source_type      TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),
    transaction_type TEXT NOT NULL,
    direction        TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral')),
    linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),
    amount           DECIMAL(18,2) NOT NULL,
    price_per_unit   DECIMAL(18,4),
    quantity         DECIMAL(18,4),
    fee              DECIMAL(18,2) DEFAULT 0,
    currency         TEXT DEFAULT 'CNY',
    transaction_date TIMESTAMP NOT NULL,
    note             TEXT,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfers (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id BIGINT NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    to_asset_id   BIGINT NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS tags (
    id         BIGSERIAL PRIMARY KEY,
    user_id    BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    color      TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, name)
);

CREATE TABLE IF NOT EXISTS expense_tags (
    expense_id BIGINT NOT NULL REFERENCES daily_expenses(id) ON DELETE CASCADE,
    tag_id     BIGINT NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (expense_id, tag_id)
);

CREATE TABLE IF NOT EXISTS daily_expenses (
    id           BIGSERIAL PRIMARY KEY,
    user_id      BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id  BIGINT NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    asset_id     BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    expense_type TEXT NOT NULL CHECK(expense_type IN ('expense', 'income')),
    amount       DECIMAL(18,2) NOT NULL,
    currency     TEXT DEFAULT 'CNY',
    expense_date DATE NOT NULL,
    note         TEXT,
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_daily_expenses_date ON daily_expenses(expense_date);
CREATE INDEX IF NOT EXISTS idx_daily_expenses_type ON daily_expenses(expense_type);
CREATE INDEX IF NOT EXISTS idx_daily_expenses_cat ON daily_expenses(category_id);
CREATE INDEX IF NOT EXISTS idx_tags_user ON tags(user_id);
CREATE INDEX IF NOT EXISTS idx_expense_tags_tag ON expense_tags(tag_id);

-- 资产余额审计日志（只增不删；asset_id 不设外键——删资产后日志保留）
CREATE TABLE IF NOT EXISTS asset_balance_logs (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    asset_id      BIGINT NOT NULL,
    user_id       BIGINT NOT NULL REFERENCES users(id),
    delta         DECIMAL(18,2) NOT NULL,
    balance_after DECIMAL(18,2) NOT NULL,
    source_type   TEXT NOT NULL,
    source_id     BIGINT NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_balance_logs_asset ON asset_balance_logs(asset_id, created_at);
CREATE INDEX IF NOT EXISTS idx_categories_user_parent ON categories(user_id, parent_id);
CREATE INDEX IF NOT EXISTS idx_categories_user_type ON categories(user_id, type);

-- 默认分类种子状态（每个用户只种一次）
CREATE TABLE IF NOT EXISTS category_seed_state (
    user_id    BIGINT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    seeded_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
