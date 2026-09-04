-- V006: 周期定投计划与现金流日程管理 (PostgreSQL)
CREATE TABLE IF NOT EXISTS dca_plans (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    target_asset_id BIGINT NOT NULL REFERENCES assets(id),
    funding_asset_id BIGINT NOT NULL REFERENCES assets(id),
    ledger_id BIGINT,
    name TEXT NOT NULL,
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly',
    day_of_period INTEGER NOT NULL DEFAULT 1,
    amount DECIMAL(18,2) NOT NULL,
    target_profit_rate DECIMAL(8,4) DEFAULT 0,
    target_total_amount DECIMAL(18,2) DEFAULT 0,
    target_total_periods INTEGER DEFAULT 0,
    status VARCHAR(32) NOT NULL DEFAULT 'active',
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_dca_plans_user_status ON dca_plans(user_id, status);
CREATE INDEX IF NOT EXISTS idx_dca_plans_ledger ON dca_plans(ledger_id);

CREATE TABLE IF NOT EXISTS dca_executions (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT NOT NULL REFERENCES dca_plans(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id),
    period_date VARCHAR(32) NOT NULL,
    planned_amount DECIMAL(18,2) NOT NULL,
    actual_amount DECIMAL(18,2) DEFAULT 0,
    executed_price DECIMAL(18,4) DEFAULT 0,
    executed_quantity DECIMAL(18,4) DEFAULT 0,
    transaction_id BIGINT DEFAULT NULL REFERENCES transactions(id),
    status VARCHAR(32) NOT NULL DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_dca_exec_plan_period ON dca_executions(plan_id, period_date);
CREATE INDEX IF NOT EXISTS idx_dca_exec_user_pending ON dca_executions(user_id, status);

CREATE TABLE IF NOT EXISTS cashflow_schedules (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    source_asset_id BIGINT NOT NULL REFERENCES assets(id),
    target_asset_id BIGINT NOT NULL REFERENCES assets(id),
    ledger_id BIGINT,
    name TEXT NOT NULL,
    flow_type VARCHAR(32) NOT NULL DEFAULT 'dividend',
    frequency VARCHAR(32) NOT NULL DEFAULT 'monthly',
    start_date VARCHAR(32) NOT NULL,
    end_date VARCHAR(32) DEFAULT '',
    expected_amount DECIMAL(18,2) NOT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'active',
    note TEXT DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_user ON cashflow_schedules(user_id, status);
CREATE INDEX IF NOT EXISTS idx_cashflow_schedules_ledger ON cashflow_schedules(ledger_id);
