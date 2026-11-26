-- V037: iLink 定时任务推送持久化 Outbox 队列与入站会话活跃时间戳 (PostgreSQL)

ALTER TABLE im_bot_instances ADD COLUMN IF NOT EXISTS last_inbound_at TIMESTAMPTZ;
ALTER TABLE job_run_logs ADD COLUMN IF NOT EXISTS notif_sync_sent INTEGER NOT NULL DEFAULT 0;
ALTER TABLE job_run_logs ADD COLUMN IF NOT EXISTS notif_sync_failed INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS im_push_outbox (
    id                 BIGSERIAL PRIMARY KEY,
    instance_id        BIGINT NOT NULL REFERENCES im_bot_instances(id) ON DELETE CASCADE,
    mf_user_id         BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    job_run_log_id     BIGINT REFERENCES job_run_logs(id) ON DELETE SET NULL,
    kind               VARCHAR(16) NOT NULL DEFAULT 'scheduler',
    title              TEXT NOT NULL,
    content            TEXT NOT NULL,
    status             VARCHAR(16) NOT NULL DEFAULT 'pending',
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 5,
    lease_until_ts     BIGINT NOT NULL DEFAULT 0,
    next_attempt_at_ts BIGINT NOT NULL DEFAULT 0,
    expires_at_ts      BIGINT NOT NULL,
    last_error         TEXT NOT NULL DEFAULT '',
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_im_push_outbox_claim
    ON im_push_outbox(instance_id, status, next_attempt_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_expires
    ON im_push_outbox(status, expires_at_ts);
CREATE INDEX IF NOT EXISTS idx_im_push_outbox_job_log
    ON im_push_outbox(job_run_log_id) WHERE job_run_log_id IS NOT NULL;
