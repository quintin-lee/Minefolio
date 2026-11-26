-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (SQLite)

ALTER TABLE im_bot_instances ADD COLUMN last_inbound_at TEXT;
ALTER TABLE job_run_logs ADD COLUMN notif_sync_sent INTEGER NOT NULL DEFAULT 0;
ALTER TABLE job_run_logs ADD COLUMN notif_sync_failed INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS im_push_outbox (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    instance_id        INTEGER NOT NULL REFERENCES im_bot_instances(id) ON DELETE CASCADE,
    mf_user_id         INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_run_log_id     INTEGER REFERENCES job_run_logs(id) ON DELETE SET NULL,
    kind               TEXT NOT NULL DEFAULT 'scheduler',
    title              TEXT NOT NULL,
    content            TEXT NOT NULL,
    status             TEXT NOT NULL DEFAULT 'pending',
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 5,
    lease_until_ts     INTEGER NOT NULL DEFAULT 0,
    next_attempt_at_ts INTEGER NOT NULL DEFAULT 0,
    expires_at_ts      INTEGER NOT NULL,
    last_error         TEXT NOT NULL DEFAULT '',
    created_at         TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_im_push_outbox_claim
    ON im_push_outbox(instance_id, status, next_attempt_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_expires
    ON im_push_outbox(status, expires_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_job_log
    ON im_push_outbox(job_run_log_id) WHERE job_run_log_id IS NOT NULL;
