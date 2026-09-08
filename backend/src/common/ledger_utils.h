/**
 * @file ledger_utils.h
 * @brief 共享账本工具函数接口
 *
 * 提供核心组件所需的账本查询函数，避免直接依赖 legacy repository 文件。
 */

#pragma once

#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 获取用户的默认账本 ID（兜底自动创建）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int64_t 默认账本 ID
 */
int64_t ledger_get_default(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 获取用户在指定账本中的角色
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @param out_role 输出角色字符串缓冲区
 * @param out_len 缓冲区长度
 * @return const char* 角色字符串，若用户非成员则返回 NULL
 */
const char* ledger_get_user_role(
    csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len);

/**
 * @brief 获取交易记录（用于回滚）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @return csilk_json_t* 交易记录 JSON 数组
 */
csilk_json_t* tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 获取手续费子行（用于回滚）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易 ID
 * @return csilk_json_t* 手续费子行 JSON 数组
 */
csilk_json_t* tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);

/**
 * @brief 删除手续费子行（用于回滚）
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易 ID
 * @return int 0 成功，-1 失败
 */
int tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);
