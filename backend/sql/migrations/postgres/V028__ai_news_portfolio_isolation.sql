-- V028: AI 每日金融资讯两阶段分析与账本/持仓隔离 (PostgreSQL)
-- 1. 创建公共大盘摘要表 market_news_digests
CREATE TABLE IF NOT EXISTS market_news_digests (
    id              BIGSERIAL PRIMARY KEY,
    digest_date     TEXT NOT NULL UNIQUE,
    title           TEXT NOT NULL DEFAULT '',
    macro_summary   TEXT NOT NULL DEFAULT '',
    sector_impacts  TEXT NOT NULL DEFAULT '[]',
    sources         TEXT NOT NULL DEFAULT '[]',
    model_used      TEXT NOT NULL DEFAULT '',
    token_cost      DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_news_digests_date ON market_news_digests(digest_date DESC);

-- 2. 扩展 ai_news_reports 表字段
ALTER TABLE ai_news_reports ADD COLUMN IF NOT EXISTS ledger_id BIGINT NOT NULL DEFAULT 0;
ALTER TABLE ai_news_reports ADD COLUMN IF NOT EXISTS digest_id BIGINT NOT NULL DEFAULT 0;
ALTER TABLE ai_news_reports ADD COLUMN IF NOT EXISTS holdings_snapshot TEXT NOT NULL DEFAULT '[]';

-- 3. 回填历史数据 ledger_id
UPDATE ai_news_reports r
SET ledger_id = COALESCE((SELECT l.id FROM ledgers l WHERE l.owner_id = r.user_id AND l.is_default = 1 LIMIT 1), 0)
WHERE ledger_id = 0;

-- 4. 调整唯一约束为 UNIQUE(user_id, ledger_id, report_date)
ALTER TABLE ai_news_reports DROP CONSTRAINT IF EXISTS ai_news_reports_user_id_report_date_key;
ALTER TABLE ai_news_reports DROP CONSTRAINT IF EXISTS ai_news_reports_user_id_ledger_id_report_date_key;
ALTER TABLE ai_news_reports ADD CONSTRAINT ai_news_reports_user_id_ledger_id_report_date_key UNIQUE(user_id, ledger_id, report_date);

CREATE INDEX IF NOT EXISTS idx_news_reports_user_ledger ON ai_news_reports(user_id, ledger_id, report_date DESC);
