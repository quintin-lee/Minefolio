-- V018: ai_traces 会话维度治理
--
-- 目标：
--   1) 补 session 维度索引（trace 读取/清理路径此前无索引）；
--   2) 会话删除时级联清理 trace（旧库 FK 无 ON DELETE，删除会话会 FK 失败）。
--
-- PG 侧：V005 的 FK 约束未命名，旧库用动态目录查询删除后按 CASCADE 重建；
-- 新库建表已带 ON DELETE CASCADE（见 V018 对 sql/migration_postgres.sql 的同步），
-- 约束重建幂等跳过。baseline 旧库可能没有 ai_traces 表（V005 被跳过未执行），
-- 先补齐表壳（列集与 V005 一致，对已有库 no-op）。

CREATE TABLE IF NOT EXISTS ai_traces (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    session_id BIGINT REFERENCES ai_sessions(id) ON DELETE CASCADE,
    provider TEXT NOT NULL DEFAULT '',
    model TEXT NOT NULL DEFAULT '',
    input_messages TEXT NOT NULL DEFAULT '[]',
    output_content TEXT NOT NULL DEFAULT '',
    system_prompt TEXT NOT NULL DEFAULT '',
    prompt_tokens INTEGER DEFAULT 0,
    completion_tokens INTEGER DEFAULT 0,
    total_tokens INTEGER DEFAULT 0,
    latency_ms INTEGER DEFAULT 0,
    first_token_ms INTEGER DEFAULT 0,
    tokens_per_sec REAL DEFAULT 0,
    cost_usd REAL DEFAULT 0,
    temperature REAL DEFAULT 0,
    max_tokens INTEGER DEFAULT 0,
    top_p REAL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'ok',
    error_message TEXT NOT NULL DEFAULT '',
    metadata TEXT NOT NULL DEFAULT '{}',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ai_traces_session ON ai_traces(session_id);

DO $$
DECLARE
    conname_text TEXT;
BEGIN
    SELECT c.conname INTO conname_text
    FROM pg_constraint c
    JOIN pg_class t ON t.oid = c.conrelid
    WHERE t.relname = 'ai_traces'
      AND c.contype = 'f'
      AND pg_get_constraintdef(c.oid) LIKE '%session_id%'
      AND pg_get_constraintdef(c.oid) NOT LIKE '%CASCADE%';
    IF conname_text IS NOT NULL THEN
        EXECUTE format('ALTER TABLE ai_traces DROP CONSTRAINT %I', conname_text);
        ALTER TABLE ai_traces
            ADD CONSTRAINT ai_traces_session_del_cascade
            FOREIGN KEY (session_id) REFERENCES ai_sessions(id) ON DELETE CASCADE;
    END IF;
END $$;
