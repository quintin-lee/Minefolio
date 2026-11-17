-- V026: 汇率源权威化 (SQLite)
-- 背景: V007 向 exchange_rates 预置了 8 个主流币种的种子汇率, 该表语义本应是
-- "用户手动钉住的汇率", 但种子行冒充手动值, 导致 GET /market/exchange-rates 的
-- 读路径 (UNION 先到先得) 永远优先命中种子, 掩盖了 exchange_rate_history 中
-- 实时同步的最新汇率 (USD 7.24 vs 实时 6.66)。
--
-- 引入 manual 标记列区分来源:
--   manual = 1  -> 由用户显式 POST /market/exchange-rates 手动钉住, 读路径优先采用
--   manual = 0  -> 种子或历史残留, 读路径视为陈旧, 仅当实时同步表无该币种时兜底
-- 现存全部行 (含 V007 种子) 默认 manual = 0, 待后续手动覆盖才升级。
ALTER TABLE exchange_rates ADD COLUMN manual INTEGER NOT NULL DEFAULT 0;

-- 显式确保 V007 种子行归零 (新建库 ADD COLUMN 默认已 0, 此处对存量库做防御性收敛)
UPDATE exchange_rates SET manual = 0 WHERE manual IS NULL;
