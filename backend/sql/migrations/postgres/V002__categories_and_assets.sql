-- V002: 分类与资产账户体系 (PostgreSQL)
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

CREATE INDEX IF NOT EXISTS idx_categories_user_parent ON categories(user_id, parent_id);
CREATE INDEX IF NOT EXISTS idx_categories_user_type ON categories(user_id, type);

CREATE TABLE IF NOT EXISTS assets (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   BIGINT NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    name          TEXT NOT NULL,
    account_no    TEXT,
    symbol        TEXT DEFAULT '',
    quote_source  TEXT DEFAULT '',
    last_sync_at  TIMESTAMP,
    current_value DECIMAL(18,2) DEFAULT 0,
    quantity      DECIMAL(18,4) NOT NULL DEFAULT 0,
    cost_basis    DECIMAL(18,4) NOT NULL DEFAULT 0,
    net_value     DECIMAL(18,4) NOT NULL DEFAULT 0,
    currency      TEXT DEFAULT 'CNY',
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS asset_price_history (
    id          BIGSERIAL PRIMARY KEY,
    asset_id    BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    price_date  DATE NOT NULL,
    price       DOUBLE PRECISION NOT NULL,
    currency    VARCHAR(16) DEFAULT 'CNY',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(asset_id, price_date)
);

CREATE INDEX IF NOT EXISTS idx_price_history_asset_date 
    ON asset_price_history(asset_id, price_date DESC);
