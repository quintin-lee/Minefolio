-- V032: Unified asynchronous background tasks table (SQLite)

CREATE TABLE IF NOT EXISTS async_tasks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL,
    ledger_id     INTEGER NOT NULL DEFAULT 0,
    task_type     TEXT NOT NULL,
    status        TEXT NOT NULL CHECK(status IN ('pending', 'running', 'completed', 'failed')),
    progress      INTEGER NOT NULL DEFAULT 0,
    message       TEXT NOT NULL DEFAULT '',
    result_json   TEXT NOT NULL DEFAULT '',
    error_message TEXT NOT NULL DEFAULT '',
    dedupe_key    TEXT NOT NULL DEFAULT '',
    created_at    TEXT DEFAULT CURRENT_TIMESTAMP,
    started_at    TEXT DEFAULT '',
    completed_at  TEXT DEFAULT '',
    updated_at    TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_async_tasks_user_status
    ON async_tasks(user_id, status, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_async_tasks_dedupe
    ON async_tasks(user_id, dedupe_key, status);
