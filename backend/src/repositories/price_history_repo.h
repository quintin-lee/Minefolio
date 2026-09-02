#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file price_history_repo.h
 * @brief 投资资产历史价格走势 (Asset Price History) 数据访问层接口
 *
 * 负责记录每日行情收盘价格/单位净值快照（用于收益走势图和历史估值回溯），
 * 支持跨数据库方言 (SQLite/PostgreSQL) 的幂等写入 (UPSERT) 及走势序列查询。
 */

/**
 * @brief 记录或更新资产在特定日期的历史价格 (UPSERT)
 *
 * 当 `(asset_id, price_date)` 唯一键冲突时自动更新当日价格与货币代码。
 * 兼容 SQLite 与 PostgreSQL 的数据类型转换语法。
 *
 * @param pool 数据库连接池指针
 * @param asset_id 目标资产 ID
 * @param price_date 价格日期字符串 (YYYY-MM-DD)
 * @param price 历史单价/单位净值
 * @param currency 货币单位（若为 NULL 或空则默认 "CNY"）
 * @return int 成功返回 0，失败返回 -1
 */
int price_history_record(csilk_db_pool_t* pool,
                         int64_t          asset_id,
                         const char*      price_date,
                         double           price,
                         const char*      currency);

/**
 * @brief 查询指定资产的历史价格走势记录列表
 *
 * 联查 assets 表做用户身份隔离校验，按日期升序 (`price_date ASC`) 返回，
 * 适合前端 ECharts 折线图/K线图直接渲染。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（鉴权防越权）
 * @param asset_id 资产 ID
 * @param limit 最多返回的历史天数（如 30、90、365 等，<=0 时默认 90）
 * @return csilk_json_t* 包含 price_date, price, currency 等字段的 JSON 数组
 */
csilk_json_t*
price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit);
