-- V029: 定时任务调度中心与运行日志 (PostgreSQL)

CREATE TABLE IF NOT EXISTS scheduled_jobs (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL,
    name                VARCHAR(128) NOT NULL,
    description         TEXT DEFAULT '',
    job_type            VARCHAR(32) NOT NULL,
    trigger_type        VARCHAR(32) NOT NULL,
    trigger_config      TEXT NOT NULL,
    payload_config      TEXT NOT NULL,
    delivery_channel    VARCHAR(32) NOT NULL DEFAULT 'user_default',
    delivery_config     TEXT DEFAULT '{}',
    enabled             INTEGER NOT NULL DEFAULT 1,
    last_run_at         VARCHAR(32) DEFAULT '',
    last_run_status     VARCHAR(16) DEFAULT '',
    last_run_duration_ms INTEGER DEFAULT 0,
    next_run_at         VARCHAR(32) DEFAULT '',
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_scheduled_jobs_user_enabled 
    ON scheduled_jobs(user_id, enabled);

CREATE TABLE IF NOT EXISTS user_notification_channels (
    id                  BIGSERIAL PRIMARY KEY,
    user_id             BIGINT NOT NULL UNIQUE,
    channel_type        VARCHAR(32) NOT NULL,
    channel_name        VARCHAR(64) NOT NULL DEFAULT '个人微信',
    webhook_url         TEXT DEFAULT '',
    secret_key          TEXT DEFAULT '',
    is_active           INTEGER NOT NULL DEFAULT 1,
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS job_run_logs (
    id                  BIGSERIAL PRIMARY KEY,
    job_id              BIGINT NOT NULL REFERENCES scheduled_jobs(id) ON DELETE CASCADE,
    user_id             BIGINT NOT NULL,
    started_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at        TIMESTAMP,
    status              VARCHAR(16) NOT NULL,
    duration_ms         INTEGER NOT NULL DEFAULT 0,
    trigger_reason      VARCHAR(64) NOT NULL,
    output_summary      TEXT DEFAULT '',
    error_message       TEXT DEFAULT '',
    notification_status VARCHAR(16) DEFAULT 'none'
);

CREATE INDEX IF NOT EXISTS idx_job_run_logs_job_id 
    ON job_run_logs(job_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_job_run_logs_user_started 
    ON job_run_logs(user_id, started_at DESC);
