-- V016: ai_messages 增加 tool_calls 列存储工具调用记录 (JSON 数组)
ALTER TABLE ai_messages ADD COLUMN IF NOT EXISTS tool_calls TEXT DEFAULT '';
