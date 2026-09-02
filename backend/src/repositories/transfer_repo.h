#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file transfer_repo.h
 * @brief 账户间内部划转流水 (Transfers) 数据访问层接口定义
 *
 * 负责资金在不同资产账户之间划转（如银行卡转微信钱包、活期转定期等）的双边账户有效性前置校验与流水持久化。
 */

/**
 * @brief 校验转出与转入两个资产账户是否存在且均归属于当前用户
 *
 * 执行参数化 SQL：`SELECT COUNT(*) as cnt FROM assets WHERE id IN (?, ?) AND user_id=?`
 * 只有当统计结果 `cnt == 2` 时才判定校验通过。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（隔离校验）
 * @param from_id 转出资金资产 ID
 * @param to_id 转入资金资产 ID
 * @return int 两个资产账户均有效且归属当前用户返回 1，否则返回 0
 */
int transfer_asset_check(csilk_db_pool_t* pool, int64_t user_id, int64_t from_id, int64_t to_id);

/**
 * @brief 插入一条账户间转账流水记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param from_id 转出账户资产 ID
 * @param to_id 转入账户资产 ID
 * @param amount 划转金额
 * @param currency 结算货币（如 "CNY"）
 * @param date 转账日期 (YYYY-MM-DD)
 * @param note 备注说明
 * @return int64_t 成功返回新转账记录的主键 ID，失败返回 0
 */
int64_t transfer_insert(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        int64_t          from_id,
                        int64_t          to_id,
                        double           amount,
                        const char*      currency,
                        const char*      date,
                        const char*      note);
