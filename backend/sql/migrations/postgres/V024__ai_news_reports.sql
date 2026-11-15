-- V024: AI 每日金融资讯分析报告 (PostgreSQL)

CREATE TABLE IF NOT EXISTS ai_news_reports (
    id              BIGSERIAL PRIMARY KEY,
    user_id         BIGINT NOT NULL,
    report_date     TEXT NOT NULL,
    title           TEXT NOT NULL DEFAULT '',
    summary         TEXT NOT NULL DEFAULT '',
    analysis        TEXT NOT NULL DEFAULT '',
    affected_assets TEXT NOT NULL DEFAULT '[]',
    sources         TEXT NOT NULL DEFAULT '[]',
    model_used      TEXT NOT NULL DEFAULT '',
    token_cost      DOUBLE PRECISION NOT NULL DEFAULT 0.0,
    status          TEXT NOT NULL DEFAULT 'pending',
    error_msg       TEXT NOT NULL DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT to_char(now(), 'YYYY-MM-DD HH24:MI:SS'),
    UNIQUE(user_id, report_date)
);

CREATE INDEX IF NOT EXISTS idx_news_reports_user_date ON ai_news_reports(user_id, report_date DESC);
