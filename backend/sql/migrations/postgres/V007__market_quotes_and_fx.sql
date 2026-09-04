-- V007: 智能导入规则、汇率与多币种折算 (PostgreSQL)
CREATE TABLE IF NOT EXISTS import_rules (
    id               BIGSERIAL PRIMARY KEY,
    user_id          BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    keyword          TEXT NOT NULL,
    match_field      VARCHAR(32) NOT NULL DEFAULT 'all',
    match_type       VARCHAR(32) NOT NULL DEFAULT 'contains',
    category_id      BIGINT REFERENCES categories(id) ON DELETE SET NULL,
    target_type      VARCHAR(32) NOT NULL DEFAULT 'expense',
    priority         INTEGER NOT NULL DEFAULT 100,
    is_active        BOOLEAN NOT NULL DEFAULT TRUE,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_import_rules_user ON import_rules(user_id, priority ASC);

CREATE TABLE IF NOT EXISTS exchange_rates (
    id               BIGSERIAL PRIMARY KEY,
    base_currency    VARCHAR(16) NOT NULL,
    target_currency  VARCHAR(16) NOT NULL,
    rate             DECIMAL(18,6) NOT NULL,
    updated_at       TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (base_currency, target_currency)
);

CREATE INDEX IF NOT EXISTS idx_exchange_rates_pair ON exchange_rates(base_currency, target_currency);

INSERT INTO exchange_rates (base_currency, target_currency, rate) VALUES
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
('CNY', 'USDT', 0.137931)
ON CONFLICT (base_currency, target_currency) DO NOTHING;

CREATE TABLE IF NOT EXISTS exchange_rate_history (
    id               BIGSERIAL PRIMARY KEY,
    rate_date        VARCHAR(32) NOT NULL,
    base_currency    VARCHAR(16) NOT NULL DEFAULT 'CNY',
    target_currency  VARCHAR(16) NOT NULL,
    rate             DECIMAL(18,6) NOT NULL,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (rate_date, base_currency, target_currency)
);

CREATE INDEX IF NOT EXISTS idx_fx_history_date ON exchange_rate_history(rate_date, target_currency);
