-- V023: High-performance reporting and FX query indices
-- 1. Accelerates latest rate query and point-in-time FX lookup in exchange_rate_history
CREATE INDEX IF NOT EXISTS idx_fx_history_currency_date ON exchange_rate_history(target_currency, rate_date DESC);

-- 2. Accelerates asset transaction range filtering and initial FX rate lookup
CREATE INDEX IF NOT EXISTS idx_transactions_asset_date ON transactions(asset_id, transaction_date);

-- 3. Accelerates 30d/90d/365d trend balance unwinding from asset_balance_logs
CREATE INDEX IF NOT EXISTS idx_balance_logs_user_date ON asset_balance_logs(user_id, created_at);

-- 4. Accelerates monthly, yearly, and category/tag expense aggregations
CREATE INDEX IF NOT EXISTS idx_daily_expenses_user_date ON daily_expenses(user_id, expense_date);
