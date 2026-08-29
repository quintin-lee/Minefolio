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


