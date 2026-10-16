-- Minefolio PostgreSQL 数据库初始化脚本

CREATE TABLE IF NOT EXISTS users (
    id                BIGSERIAL PRIMARY KEY,
    username          TEXT UNIQUE NOT NULL,
    password          TEXT NOT NULL,
    token_version     BIGINT NOT NULL DEFAULT 0,
    totp_secret       TEXT DEFAULT '',
    totp_enabled      BOOLEAN DEFAULT FALSE,
    totp_backup_codes TEXT DEFAULT '',
    created_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
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
    parent_tx_id     BIGINT REFERENCES transactions(id) ON DELETE CASCADE,
    currency         TEXT DEFAULT 'CNY',
    transaction_date TIMESTAMP NOT NULL,
    note             TEXT,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfers (
    id            BIGSERIAL PRIMARY KEY,
    token_version BIGINT NOT NULL DEFAULT 0,
    user_id       BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    to_asset_id   BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    ledger_id     BIGINT REFERENCES ledgers(id) ON DELETE SET NULL,
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

CREATE TABLE IF NOT EXISTS ai_sessions (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    title TEXT NOT NULL DEFAULT '新对话',
    model TEXT NOT NULL DEFAULT 'deepseek-chat',
    provider TEXT NOT NULL DEFAULT 'deepseek',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ai_messages (
    id BIGSERIAL PRIMARY KEY,
    session_id BIGINT NOT NULL REFERENCES ai_sessions(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK(role IN ('user', 'assistant')),
    content TEXT NOT NULL,
    model TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_messages_session ON ai_messages(session_id, created_at);

CREATE TABLE IF NOT EXISTS ai_traces (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    session_id BIGINT REFERENCES ai_sessions(id),
    provider TEXT NOT NULL DEFAULT '',
    model TEXT NOT NULL DEFAULT '',
    input_messages TEXT NOT NULL DEFAULT '[]',
    output_content TEXT NOT NULL DEFAULT '',
    system_prompt TEXT NOT NULL DEFAULT '',
    prompt_tokens INTEGER DEFAULT 0,
    completion_tokens INTEGER DEFAULT 0,
    total_tokens INTEGER DEFAULT 0,
    latency_ms INTEGER DEFAULT 0,
    first_token_ms INTEGER DEFAULT 0,
    tokens_per_sec DOUBLE PRECISION DEFAULT 0,
    cost_usd DOUBLE PRECISION DEFAULT 0,
    temperature DOUBLE PRECISION DEFAULT 0,
    max_tokens INTEGER DEFAULT 0,
    top_p DOUBLE PRECISION DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ok',
    error_message TEXT NOT NULL DEFAULT '',
    metadata TEXT NOT NULL DEFAULT '{}',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_traces_user ON ai_traces(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_ai_traces_provider ON ai_traces(user_id, provider);
CREATE INDEX IF NOT EXISTS idx_ai_traces_model ON ai_traces(user_id, model);

CREATE TABLE IF NOT EXISTS ai_settings (
    id INTEGER PRIMARY KEY CHECK(id = 1),
    config_json TEXT NOT NULL DEFAULT '{}',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 定投计划表
CREATE TABLE IF NOT EXISTS dca_plans (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    target_asset_id BIGINT NOT NULL REFERENCES assets(id),
    funding_asset_id BIGINT NOT NULL REFERENCES assets(id),
    ledger_id BIGINT,
    name VARCHAR(128) NOT NULL,
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly', -- 'weekly', 'biweekly', 'monthly'
    day_of_period INT NOT NULL DEFAULT 1,            -- 周几(1-7) 或 每月几号(1-31)
    amount DOUBLE PRECISION NOT NULL,
    target_profit_rate DOUBLE PRECISION DEFAULT 0,    -- 目标止盈率(如 0.15 表示 15%)
    target_total_amount DOUBLE PRECISION DEFAULT 0,
    target_total_periods INT DEFAULT 0,
    status VARCHAR(32) NOT NULL DEFAULT 'active',     -- 'active', 'paused', 'completed'
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_dca_plans_user_status ON dca_plans(user_id, status);
CREATE INDEX IF NOT EXISTS idx_dca_plans_ledger ON dca_plans(ledger_id);

-- 定投执行记录与待办
CREATE TABLE IF NOT EXISTS dca_executions (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT NOT NULL REFERENCES dca_plans(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    period_date VARCHAR(16) NOT NULL,                -- YYYY-MM-DD
    planned_amount DOUBLE PRECISION NOT NULL,
    actual_amount DOUBLE PRECISION DEFAULT 0,
    executed_price DOUBLE PRECISION DEFAULT 0,
    executed_quantity DOUBLE PRECISION DEFAULT 0,
    transaction_id BIGINT DEFAULT NULL REFERENCES transactions(id),
    status VARCHAR(32) NOT NULL DEFAULT 'pending',   -- 'pending', 'confirmed', 'skipped'
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_dca_exec_plan_period ON dca_executions(plan_id, period_date);
CREATE INDEX IF NOT EXISTS idx_dca_exec_user_pending ON dca_executions(user_id, status);

-- 周期性被动现金流计划表
CREATE TABLE IF NOT EXISTS cashflow_schedules (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    source_asset_id BIGINT NOT NULL REFERENCES assets(id),
    target_asset_id BIGINT NOT NULL REFERENCES assets(id),
    ledger_id BIGINT,
    name VARCHAR(128) NOT NULL,
    flow_type VARCHAR(32) NOT NULL DEFAULT 'dividend', -- 'dividend', 'interest', 'rent', 'maturity'
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly',  -- 'once', 'monthly', 'quarterly', 'semi_annual', 'annual'
    start_date VARCHAR(16) NOT NULL,                 -- YYYY-MM-DD
    end_date VARCHAR(16) DEFAULT '',                 -- YYYY-MM-DD (可选)
    expected_amount DOUBLE PRECISION NOT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'active',    -- 'active', 'completed', 'cancelled'
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_user ON cashflow_schedules(user_id, status);
CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_ledger ON cashflow_schedules(ledger_id);

-- 账本空间主表
CREATE TABLE IF NOT EXISTS ledgers (
    id BIGSERIAL PRIMARY KEY,
    owner_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(128) NOT NULL,
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

-- 账本成员关系表
CREATE TABLE IF NOT EXISTS ledger_members (
    id BIGSERIAL PRIMARY KEY,
    ledger_id BIGINT NOT NULL REFERENCES ledgers(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role VARCHAR(32) NOT NULL CHECK (role IN ('owner', 'editor', 'viewer')),
    joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (ledger_id, user_id)
);
CREATE INDEX IF NOT EXISTS idx_ledger_members_user ON ledger_members(user_id);
CREATE INDEX IF NOT EXISTS idx_ledger_members_ledger ON ledger_members(ledger_id);

-- 业务表 ledger_id 关联
ALTER TABLE assets ADD COLUMN IF NOT EXISTS ledger_id BIGINT;
ALTER TABLE transactions ADD COLUMN IF NOT EXISTS ledger_id BIGINT;
ALTER TABLE daily_expenses ADD COLUMN IF NOT EXISTS ledger_id BIGINT;
ALTER TABLE categories ADD COLUMN IF NOT EXISTS ledger_id BIGINT;

CREATE INDEX IF NOT EXISTS idx_assets_ledger ON assets(ledger_id);
CREATE INDEX IF NOT EXISTS idx_transactions_ledger ON transactions(ledger_id);
CREATE INDEX IF NOT EXISTS idx_daily_expenses_ledger ON daily_expenses(ledger_id);
CREATE INDEX IF NOT EXISTS idx_categories_ledger ON categories(ledger_id);

-- 账单导入智能规则表
CREATE TABLE IF NOT EXISTS import_rules (
    id               BIGSERIAL PRIMARY KEY,
    user_id          BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    keyword          TEXT NOT NULL,
    match_field      VARCHAR(32) NOT NULL DEFAULT 'all' CHECK(match_field IN ('all','description','counterparty','note')),
    match_type       VARCHAR(32) NOT NULL DEFAULT 'contains' CHECK(match_type IN ('contains','exact','regex')),
    category_id      BIGINT REFERENCES categories(id) ON DELETE SET NULL,
    target_type      VARCHAR(32) NOT NULL DEFAULT 'expense' CHECK(target_type IN ('expense','income','transaction')),
    priority         INTEGER NOT NULL DEFAULT 100,
    is_active        BOOLEAN NOT NULL DEFAULT TRUE,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_import_rules_user ON import_rules(user_id, priority ASC);

-- 汇率与多币种折算表
CREATE TABLE IF NOT EXISTS exchange_rates (
    id               BIGSERIAL PRIMARY KEY,
    base_currency    VARCHAR(10) NOT NULL,
    target_currency  VARCHAR(10) NOT NULL,
    rate             DOUBLE PRECISION NOT NULL,
    updated_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uq_exchange_rates UNIQUE (base_currency, target_currency)
);
CREATE INDEX IF NOT EXISTS idx_exchange_rates_pair ON exchange_rates(base_currency, target_currency);

-- 汇率历史走势快照表
CREATE TABLE IF NOT EXISTS exchange_rate_history (
    id               BIGSERIAL PRIMARY KEY,
    rate_date        VARCHAR(10) NOT NULL,
    base_currency    VARCHAR(10) NOT NULL DEFAULT 'CNY',
    target_currency  VARCHAR(10) NOT NULL,
    rate             DOUBLE PRECISION NOT NULL,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uq_fx_history UNIQUE (rate_date, base_currency, target_currency)
);
CREATE INDEX IF NOT EXISTS idx_fx_history_date ON exchange_rate_history(rate_date, target_currency);

-- 全球大盘指数持久化表
CREATE TABLE IF NOT EXISTS market_indices (
    symbol          VARCHAR(32) PRIMARY KEY,
    name            VARCHAR(64) NOT NULL,
    category        VARCHAR(32) NOT NULL,
    region_flag     VARCHAR(16) NOT NULL,
    current_price   DOUBLE PRECISION NOT NULL DEFAULT 0,
    change_amount   DOUBLE PRECISION NOT NULL DEFAULT 0,
    change_percent  DOUBLE PRECISION NOT NULL DEFAULT 0,
    prev_close      DOUBLE PRECISION NOT NULL DEFAULT 0,
    open_price      DOUBLE PRECISION NOT NULL DEFAULT 0,
    high_price      DOUBLE PRECISION NOT NULL DEFAULT 0,
    low_price       DOUBLE PRECISION NOT NULL DEFAULT 0,
    currency        VARCHAR(16) NOT NULL DEFAULT 'CNY',
    quote_time      VARCHAR(32) NOT NULL DEFAULT '',
    sparkline       TEXT NOT NULL DEFAULT '[]',
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_market_indices_cat ON market_indices(category);

INSERT INTO market_indices (symbol, name, category, region_flag, current_price, change_amount, change_percent, prev_close, open_price, high_price, low_price, currency, quote_time, sparkline) VALUES
('sh000001', '上证指数', 'cn', '🇨🇳', 3285.40, 25.40, 0.78, 3260.00, 3270.00, 3295.00, 3265.00, 'CNY', '2026-10-16 15:00:00', '[3265,3270,3275,3280,3278,3282,3284,3285.4]'),
('sz399001', '深证成指', 'cn', '🇨🇳', 10550.20, 54.50, 0.52, 10495.70, 10510.00, 10580.00, 10500.00, 'CNY', '2026-10-16 15:00:00', '[10500,10510,10520,10540,10530,10545,10548,10550.2]'),
('sz399006', '创业板指', 'cn', '🇨🇳', 2188.60, 24.80, 1.15, 2163.80, 2170.00, 2195.00, 2168.00, 'CNY', '2026-10-16 15:00:00', '[2168,2170,2175,2180,2178,2184,2186,2188.6]'),
('sh000300', '沪深300', 'cn', '🇨🇳', 3920.10, 25.20, 0.65, 3894.90, 3905.00, 3930.00, 3900.00, 'CNY', '2026-10-16 15:00:00', '[3900,3905,3910,3915,3912,3918,3919,3920.1]'),
('hkHSI', '恒生指数', 'hk', '🇭🇰', 20630.00, -51.80, -0.25, 20681.80, 20650.00, 20720.00, 20600.00, 'HKD', '2026-10-16 16:00:00', '[20680,20670,20650,20640,20620,20630,20635,20630]'),
('hkHSTECH', '恒生科技', 'hk', '🇭🇰', 4520.50, 18.90, 0.42, 4501.60, 4510.00, 4535.00, 4495.00, 'HKD', '2026-10-16 16:00:00', '[4500,4510,4515,4520,4512,4518,4522,4520.5]'),
('^GSPC', '标普 500', 'us', '🇺🇸', 5864.67, 24.50, 0.42, 5840.17, 5845.00, 5875.00, 5835.00, 'USD', '2026-10-16 16:00:00', '[5840,5845,5850,5855,5860,5862,5863,5864.67]'),
('^IXIC', '纳斯达克', 'us', '🇺🇸', 18373.61, -33.10, -0.18, 18406.71, 18390.00, 18420.00, 18350.00, 'USD', '2026-10-16 16:00:00', '[18400,18390,18385,18370,18365,18370,18372,18373.61]'),
('^DJI', '道琼斯', 'us', '🇺🇸', 43065.22, 150.20, 0.35, 42915.02, 42950.00, 43120.00, 42920.00, 'USD', '2026-10-16 16:00:00', '[42920,42950,42980,43010,43030,43050,43060,43065.22]'),
('^N225', '日经 225', 'global', '🇯🇵', 39180.30, 341.20, 0.88, 38839.10, 38900.00, 39250.00, 38880.00, 'JPY', '2026-10-16 15:00:00', '[38850,38900,38980,39050,39120,39150,39170,39180.3]'),
('^GDAXI', '德国 DAX', 'global', '🇩🇪', 19580.40, 41.10, 0.21, 19539.30, 19550.00, 19610.00, 19530.00, 'EUR', '2026-10-16 17:30:00', '[19540,19550,19560,19570,19565,19575,19578,19580.4]'),
('^FTSE', '英国富时', 'global', '🇬🇧', 8240.10, -4.10, -0.05, 8244.20, 8250.00, 8265.00, 8230.00, 'GBP', '2026-10-16 16:30:00', '[8245,8250,8248,8242,8238,8240,8239,8240.1]'),
('BTC-USD', '比特币', 'crypto', '🪙', 67800.00, 1620.00, 2.45, 66180.00, 66200.00, 68200.00, 66100.00, 'USD', '2026-10-16 15:30:00', '[66200,66500,66800,67100,67400,67600,67750,67800]'),
('ETH-USD', '以太坊', 'crypto', '🪙', 2640.00, 47.20, 1.82, 2592.80, 2600.00, 2660.00, 2590.00, 'USD', '2026-10-16 15:30:00', '[2595,2605,2615,2625,2630,2635,2638,2640]'),
('GC=F', '黄金现货', 'crypto', '🪙', 2680.50, 10.10, 0.38, 2670.40, 2672.00, 2688.00, 2670.00, 'USD', '2026-10-16 15:30:00', '[2671,2673,2675,2678,2676,2679,2680,2680.5]')
ON CONFLICT (symbol) DO NOTHING;
