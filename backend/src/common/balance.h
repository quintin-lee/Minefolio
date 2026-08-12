#pragma once
#include "csilk/drivers/db.h"
#include <stdint.h>

/**
 * @brief 对资产余额应用增减，并写入审计日志。
 *
 * delta 为业务方向金额（收入/入金为正，支出/出金为负），函数内部根据资产
 * 类型（负债方向反转）归一化后更新 current_value，并记录 balance_after 快照。
 *
 * @param pool        数据库连接池
 * @param asset_id    目标资产 id
 * @param user_id     操作者（审计 + 归属校验）
 * @param delta       业务方向金额（正=增加余额，负=减少余额）
 * @param source_type "daily_expense" 或 "transaction"
 * @param source_id   对应主记录 id
 * @param note        冗余描述（可为 NULL）
 * @return 0 成功；-1 资产不存在或不属于该用户；-2 数据库错误
 */
int balance_apply_delta(csilk_db_pool_t* pool,
                        int64_t asset_id, int64_t user_id, double delta,
                        const char* source_type, int64_t source_id,
                        const char* note);

/** @brief 判断资产类型是否为负债（方向反转）。1=普通资产，-1=负债。 */
int balance_direction(const char* asset_type);
