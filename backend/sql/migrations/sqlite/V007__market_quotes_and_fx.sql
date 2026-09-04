-- V007: 智能导入规则、汇率与多币种折算
CREATE TABLE IF NOT EXISTS import_rules (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    keyword          TEXT NOT NULL,
    match_field      TEXT NOT NULL DEFAULT 'all' CHECK(match_field IN ('all','description','counterparty','note')),
    match_type       TEXT NOT NULL DEFAULT 'contains' CHECK(match_type IN ('contains','exact','regex')),
    category_id      INTEGER REFERENCES categories(id) ON DELETE SET NULL,
    target_type      TEXT NOT NULL DEFAULT 'expense' CHECK(target_type IN ('expense','income','transaction')),
    priority         INTEGER NOT NULL DEFAULT 100,
    is_active        BOOLEAN NOT NULL DEFAULT 1,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_import_rules_user ON import_rules(user_id, priority ASC);

CREATE TABLE IF NOT EXISTS exchange_rates (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    base_currency    TEXT NOT NULL,
    target_currency  TEXT NOT NULL,
    rate             DECIMAL(18,6) NOT NULL,
    updated_at       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (base_currency, target_currency)
);

CREATE INDEX IF NOT EXISTS idx_exchange_rates_pair ON exchange_rates(base_currency, target_currency);

INSERT OR IGNORE INTO exchange_rates (base_currency, target_currency, rate) VALUES
('USD', 'CNY', 7.240000),
('CNY', 'USD', 0.138122),
('EUR', 'CNY', 7.850000),
('CNY', 'EUR', 0.127389),
('HKD', 'CNY', 0.925000),
('CNY', 'HKD', 1.081081),
('JPY', 'CNY', 0.048000),
('CNY', 'JPY', 20.833333),
('GBP', 'CNY', 9.180000),
('CNY', 'GBP', 0.108932),
('USDT', 'CNY', 7.250000),
('CNY', 'USDT', 0.137931);

CREATE TABLE IF NOT EXISTS exchange_rate_history (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    rate_date        TEXT NOT NULL,
    base_currency    TEXT NOT NULL DEFAULT 'CNY',
    target_currency  TEXT NOT NULL,
    rate             DECIMAL(18,6) NOT NULL,
    created_at       DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (rate_date, base_currency, target_currency)
);

CREATE INDEX IF NOT EXISTS idx_fx_history_date ON exchange_rate_history(rate_date, target_currency);
