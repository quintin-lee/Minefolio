-- V028: AI 每日金融资讯两阶段分析与账本/持仓隔离 (SQLite)
-- 1. 创建公共大盘摘要表 market_news_digests (全局共享，无隐私数据)
CREATE TABLE IF NOT EXISTS market_news_digests (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    digest_date     TEXT NOT NULL UNIQUE,              -- 'YYYY-MM-DD'
    title           TEXT NOT NULL DEFAULT '',
    macro_summary   TEXT NOT NULL DEFAULT '',
    sector_impacts  TEXT NOT NULL DEFAULT '[]',         -- JSON array: [{"sector":"新能源","trend":"利好","reason":"..."}]
    sources         TEXT NOT NULL DEFAULT '[]',         -- JSON array: [{title, url, snippet}]
    model_used      TEXT NOT NULL DEFAULT '',
    token_cost      REAL NOT NULL DEFAULT 0.0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_news_digests_date ON market_news_digests(digest_date DESC);

-- 2. 重建 ai_news_reports 表，引入 ledger_id 与 digest_id，并设立 UNIQUE(user_id, ledger_id, report_date)
CREATE TABLE IF NOT EXISTS ai_news_reports_v2 (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id           INTEGER NOT NULL,
    ledger_id         INTEGER NOT NULL DEFAULT 0,
    report_date       TEXT NOT NULL,
    digest_id         INTEGER NOT NULL DEFAULT 0,
    title             TEXT NOT NULL DEFAULT '',
    summary           TEXT NOT NULL DEFAULT '',
    analysis          TEXT NOT NULL DEFAULT '',
    holdings_snapshot TEXT NOT NULL DEFAULT '[]',       -- 脱敏后的持仓占比快照 [{"name":"...","category":"...","weight":"30.5%"}]
    affected_assets   TEXT NOT NULL DEFAULT '[]',       -- JSON array [{asset_id, name, direction, impact_pct}]
    sources           TEXT NOT NULL DEFAULT '[]',
    model_used        TEXT NOT NULL DEFAULT '',
    token_cost        REAL NOT NULL DEFAULT 0.0,
    status            TEXT NOT NULL DEFAULT 'pending',  -- 'pending' | 'done' | 'error'
    error_msg         TEXT NOT NULL DEFAULT '',
    created_at        TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(user_id, ledger_id, report_date)
);

-- 3. 历史数据平滑回填
INSERT INTO ai_news_reports_v2 (
    id, user_id, ledger_id, report_date, digest_id, title, summary,
    analysis, holdings_snapshot, affected_assets, sources, model_used,
    token_cost, status, error_msg, created_at
)
SELECT
    r.id, r.user_id,
    COALESCE((SELECT l.id FROM ledgers l WHERE l.owner_id = r.user_id AND l.is_default = 1 LIMIT 1), 0),
    r.report_date, 0, r.title, r.summary,
    r.analysis, '[]', r.affected_assets, r.sources, r.model_used,
    r.token_cost, r.status, r.error_msg, r.created_at
FROM ai_news_reports r;

DROP TABLE ai_news_reports;
ALTER TABLE ai_news_reports_v2 RENAME TO ai_news_reports;

CREATE INDEX IF NOT EXISTS idx_news_reports_user_ledger ON ai_news_reports(user_id, ledger_id, report_date DESC);
