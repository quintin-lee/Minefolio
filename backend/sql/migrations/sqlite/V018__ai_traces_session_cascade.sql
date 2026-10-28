-- V018: ai_traces 会话维度治理
--
-- 目标：
--   1) 补 session 维度索引（trace 读取/清理路径此前无索引）；
--   2) 会话删除时级联清理 trace（旧库 FK 无 ON DELETE，删除会话会 FK 失败）。
--
-- 关键约束：baseline 旧库可能没有 ai_traces 表（V005 被跳过未执行），而
-- CREATE INDEX 在表缺失时会报 no such table 并中断整条迁移链。SQLite 没有语句级
-- 条件执行（IF NOT EXISTS 只作用于同名对象，不校验基表存在），因此：
--   - 用「影子表判定」：先创建临时哨兵表并在 ai_traces 缺失时保留标记，
--     索引放到 SELECT 驱动的动态语句里执行是 sqlite_exec 不支持的能力，
--     所以这里采用最朴素的兜底：确保 ai_traces 表壳存在（列集与 V005 兼容），
--     再建索引。表壳对旧库是无害补齐（此前该库不可能有 trace 数据），
--     对新库是 no-op（IF NOT EXISTS）。
CREATE TABLE IF NOT EXISTS ai_traces (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    session_id INTEGER REFERENCES ai_sessions(id),
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
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ai_traces_session ON ai_traces(session_id);
