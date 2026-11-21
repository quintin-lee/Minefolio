-- V032: Unified asynchronous background tasks table (PostgreSQL)

CREATE TABLE IF NOT EXISTS async_tasks (
    id            BIGSERIAL PRIMARY KEY,
    user_id       BIGINT NOT NULL,
    ledger_id     BIGINT NOT NULL DEFAULT 0,
    task_type     VARCHAR(64) NOT NULL,
    status        VARCHAR(16) NOT NULL CHECK(status IN ('pending', 'running', 'completed', 'failed')),
    progress      INTEGER NOT NULL DEFAULT 0,
    message       TEXT NOT NULL DEFAULT '',
    result_json   TEXT NOT NULL DEFAULT '',
    error_message TEXT NOT NULL DEFAULT '',
    dedupe_key    VARCHAR(128) NOT NULL DEFAULT '',
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at    VARCHAR(32) NOT NULL DEFAULT '',
    completed_at  VARCHAR(32) NOT NULL DEFAULT '',
    updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_async_tasks_user_status
    ON async_tasks(user_id, status, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_async_tasks_dedupe
    ON async_tasks(user_id, dedupe_key, status);
