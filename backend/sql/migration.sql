-- Minefolio 数据库初始化脚本

CREATE TABLE IF NOT EXISTS users (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    username          TEXT UNIQUE NOT NULL,
    password          TEXT NOT NULL,
    token_version     INTEGER NOT NULL DEFAULT 0,
    totp_secret       TEXT DEFAULT '',
    totp_enabled      BOOLEAN DEFAULT 0,
    totp_backup_codes TEXT DEFAULT '',
    oauth_provider    TEXT DEFAULT '',
    oauth_id          TEXT DEFAULT '',
    created_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS categories (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    parent_id  INTEGER REFERENCES categories(id) ON DELETE SET NULL,
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
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
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
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id    INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    price_date  DATE NOT NULL,
    price       DECIMAL(18,4) NOT NULL,
    currency    TEXT DEFAULT 'CNY',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(asset_id, price_date)
);

CREATE INDEX IF NOT EXISTS idx_price_history_asset_date 
    ON asset_price_history(asset_id, price_date DESC);

CREATE TABLE IF NOT EXISTS transactions (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id          INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    asset_id         INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    linked_asset_id  INTEGER REFERENCES assets(id) ON DELETE SET NULL,
    category_id      INTEGER REFERENCES categories(id) ON DELETE RESTRICT,
    source_type      TEXT NOT NULL DEFAULT 'expense' CHECK(source_type IN ('income', 'expense')),
    transaction_type TEXT NOT NULL,
    direction        TEXT NOT NULL DEFAULT 'out' CHECK(direction IN ('in','out','neutral')),
    linked_direction TEXT CHECK(linked_direction IN ('in','out','neutral')),
    amount           DECIMAL(18,2) NOT NULL,
    price_per_unit   DECIMAL(18,4),
    quantity         DECIMAL(18,4),
    fee              DECIMAL(18,2) DEFAULT 0,
    parent_tx_id     INTEGER REFERENCES transactions(id) ON DELETE CASCADE,
    currency         TEXT DEFAULT 'CNY',
    transaction_date TIMESTAMP NOT NULL,
    note             TEXT,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS transfers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    from_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    to_asset_id   INTEGER NOT NULL REFERENCES assets(id) ON DELETE RESTRICT,
    amount        DECIMAL(18,2) NOT NULL,
    currency      TEXT DEFAULT 'CNY',
    transfer_date TIMESTAMP NOT NULL,
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS tags (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    color      TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, name)
);

CREATE TABLE IF NOT EXISTS expense_tags (
    expense_id INTEGER NOT NULL REFERENCES daily_expenses(id) ON DELETE CASCADE,
    tag_id     INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (expense_id, tag_id)
);

CREATE TABLE IF NOT EXISTS daily_expenses (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id  INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    asset_id     INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
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
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id      INTEGER NOT NULL,
    user_id       INTEGER NOT NULL REFERENCES users(id),
    delta         DECIMAL(18,2) NOT NULL,
    balance_after DECIMAL(18,2) NOT NULL,
    source_type   TEXT NOT NULL,
    source_id     INTEGER NOT NULL,
    note          TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_balance_logs_asset ON asset_balance_logs(asset_id, created_at);
CREATE INDEX IF NOT EXISTS idx_categories_user_parent ON categories(user_id, parent_id);
CREATE INDEX IF NOT EXISTS idx_categories_user_type ON categories(user_id, type);

-- 默认分类种子状态（每个用户只种一次；老账号懒加载时顺带做旧名迁移）
CREATE TABLE IF NOT EXISTS category_seed_state (
    user_id    INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    seeded_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- AI chat: sessions and messages
CREATE TABLE IF NOT EXISTS ai_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    title TEXT NOT NULL DEFAULT '新对话',
    model TEXT NOT NULL DEFAULT 'deepseek-chat',
    provider TEXT NOT NULL DEFAULT 'deepseek',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ai_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES ai_sessions(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK(role IN ('user', 'assistant')),
    content TEXT NOT NULL,
    model TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_messages_session ON ai_messages(session_id, created_at);

-- AI conversation traces (for debugging & optimization)
CREATE TABLE IF NOT EXISTS ai_traces (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    session_id INTEGER REFERENCES ai_sessions(id),
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
    tokens_per_sec REAL DEFAULT 0,
    cost_usd REAL DEFAULT 0,
    temperature REAL DEFAULT 0,
    max_tokens INTEGER DEFAULT 0,
    top_p REAL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ok',
    error_message TEXT NOT NULL DEFAULT '',
    metadata TEXT NOT NULL DEFAULT '{}',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_traces_user ON ai_traces(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_ai_traces_provider ON ai_traces(user_id, provider);
CREATE INDEX IF NOT EXISTS idx_ai_traces_model ON ai_traces(user_id, model);

-- AI provider/model configuration (persists across restarts)
CREATE TABLE IF NOT EXISTS ai_settings (
    id INTEGER PRIMARY KEY CHECK(id = 1),
    config_json TEXT NOT NULL DEFAULT '{}',
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 定投计划表
CREATE TABLE IF NOT EXISTS dca_plans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    target_asset_id INTEGER NOT NULL REFERENCES assets(id),
    funding_asset_id INTEGER NOT NULL REFERENCES assets(id),
    name TEXT NOT NULL,
    frequency TEXT NOT NULL DEFAULT 'monthly', -- 'weekly', 'biweekly', 'monthly'
    day_of_period INTEGER NOT NULL DEFAULT 1,  -- 周几(1-7) 或 每月几号(1-31)
    amount DECIMAL(18,2) NOT NULL,
    target_profit_rate DECIMAL(8,4) DEFAULT 0, -- 目标止盈率(如 0.15 表示 15%)
    target_total_amount DECIMAL(18,2) DEFAULT 0,
    target_total_periods INTEGER DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'active',     -- 'active', 'paused', 'completed'
    note TEXT DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_dca_plans_user_status ON dca_plans(user_id, status);

-- 定投执行记录与待办
CREATE TABLE IF NOT EXISTS dca_executions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_id INTEGER NOT NULL REFERENCES dca_plans(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL REFERENCES users(id),
    period_date TEXT NOT NULL,                -- YYYY-MM-DD
    planned_amount DECIMAL(18,2) NOT NULL,
    actual_amount DECIMAL(18,2) DEFAULT 0,
    executed_price DECIMAL(18,4) DEFAULT 0,
    executed_quantity DECIMAL(18,4) DEFAULT 0,
    transaction_id INTEGER DEFAULT NULL REFERENCES transactions(id),
    status TEXT NOT NULL DEFAULT 'pending',   -- 'pending', 'confirmed', 'skipped'
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_dca_exec_plan_period ON dca_executions(plan_id, period_date);
CREATE INDEX IF NOT EXISTS idx_dca_exec_user_pending ON dca_executions(user_id, status);

-- 周期性被动现金流计划表
CREATE TABLE IF NOT EXISTS cashflow_schedules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    source_asset_id INTEGER NOT NULL REFERENCES assets(id),
    target_asset_id INTEGER NOT NULL REFERENCES assets(id),
    name TEXT NOT NULL,
    flow_type TEXT NOT NULL DEFAULT 'dividend', -- 'dividend', 'interest', 'rent', 'maturity'
    frequency TEXT NOT NULL DEFAULT 'monthly',  -- 'once', 'monthly', 'quarterly', 'semi_annual', 'annual'
    start_date TEXT NOT NULL,                  -- YYYY-MM-DD
    end_date TEXT DEFAULT '',                  -- YYYY-MM-DD (可选)
    expected_amount DECIMAL(18,2) NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',     -- 'active', 'completed', 'cancelled'
    note TEXT DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_user ON cashflow_schedules(user_id, status);

-- 账本空间主表
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

-- 账本成员关系表
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

-- 账单导入智能规则表
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

-- 汇率与多币种折算表
CREATE TABLE IF NOT EXISTS exchange_rates (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    base_currency    TEXT NOT NULL,
    target_currency  TEXT NOT NULL,
    rate             DECIMAL(18,6) NOT NULL,
    updated_at       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (base_currency, target_currency)
);
CREATE INDEX IF NOT EXISTS idx_exchange_rates_pair ON exchange_rates(base_currency, target_currency);

-- 默认基准汇率种子数据
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

-- 汇率历史走势快照表
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

