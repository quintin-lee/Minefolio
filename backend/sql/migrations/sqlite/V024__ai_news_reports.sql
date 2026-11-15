-- V024: AI 每日金融资讯分析报告
-- 存储由 AI 引擎定时抓取金融新闻并分析对持仓影响后生成的报告。

CREATE TABLE IF NOT EXISTS ai_news_reports (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id         INTEGER NOT NULL,
    report_date     TEXT NOT NULL,           -- 'YYYY-MM-DD' 本地时区
    title           TEXT NOT NULL DEFAULT '',
    summary         TEXT NOT NULL DEFAULT '',
    analysis        TEXT NOT NULL DEFAULT '',
    affected_assets TEXT NOT NULL DEFAULT '[]',  -- JSON array [{asset_id, name, direction, impact_pct}]
    sources         TEXT NOT NULL DEFAULT '[]',  -- JSON array [{title, url, snippet}]
    model_used      TEXT NOT NULL DEFAULT '',
    token_cost      REAL NOT NULL DEFAULT 0.0,
    status          TEXT NOT NULL DEFAULT 'pending', -- 'pending' | 'done' | 'error'
    error_msg       TEXT NOT NULL DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(user_id, report_date)
);

CREATE INDEX idx_news_reports_user_date ON ai_news_reports(user_id, report_date DESC);
