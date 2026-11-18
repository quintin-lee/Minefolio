-- V030: 定时任务内置标记支持与索引 (SQLite)

ALTER TABLE scheduled_jobs ADD COLUMN is_builtin INTEGER NOT NULL DEFAULT 0;

CREATE INDEX IF NOT EXISTS idx_scheduled_jobs_builtin 
    ON scheduled_jobs(is_builtin);
