-- V029: 定时任务调度中心与运行日志 (SQLite)

CREATE TABLE IF NOT EXISTS scheduled_jobs (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL,
    name                TEXT NOT NULL,
    description         TEXT DEFAULT '',
    job_type            TEXT NOT NULL,
    trigger_type        TEXT NOT NULL,
    trigger_config      TEXT NOT NULL,
    payload_config      TEXT NOT NULL,
    delivery_channel    TEXT NOT NULL DEFAULT 'user_default',
    delivery_config     TEXT DEFAULT '{}',
    enabled             INTEGER NOT NULL DEFAULT 1,
    is_builtin          INTEGER NOT NULL DEFAULT 0,
    last_run_at         TEXT DEFAULT '',
    last_run_status     TEXT DEFAULT '',
    last_run_duration_ms INTEGER DEFAULT 0,
    next_run_at         TEXT DEFAULT '',
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at          TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_scheduled_jobs_user_enabled 
    ON scheduled_jobs(user_id, enabled);

CREATE INDEX IF NOT EXISTS idx_scheduled_jobs_builtin 
    ON scheduled_jobs(is_builtin);

CREATE TABLE IF NOT EXISTS user_notification_channels (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id             INTEGER NOT NULL UNIQUE,
    channel_type        TEXT NOT NULL,
    channel_name        TEXT NOT NULL DEFAULT '个人微信',
    webhook_url         TEXT DEFAULT '',
    secret_key          TEXT DEFAULT '',
    is_active           INTEGER NOT NULL DEFAULT 1,
    created_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at          TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS job_run_logs (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    job_id              INTEGER NOT NULL,
    user_id             INTEGER NOT NULL,
    started_at          TEXT DEFAULT CURRENT_TIMESTAMP,
    completed_at        TEXT,
    status              TEXT NOT NULL,
    duration_ms         INTEGER NOT NULL DEFAULT 0,
    trigger_reason      TEXT NOT NULL,
    output_summary      TEXT DEFAULT '',
    error_message       TEXT DEFAULT '',
    notification_status TEXT DEFAULT 'none',
    FOREIGN KEY(job_id) REFERENCES scheduled_jobs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_job_run_logs_job_id 
    ON job_run_logs(job_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_job_run_logs_user_started 
    ON job_run_logs(user_id, started_at DESC);
