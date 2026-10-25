#pragma once

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 账户仓储抽象契约 (Domain Account Repository Contract)
 * @note 基础设施层以 SQL 实现；入参出参遵循 JSON 直返约定（无模型转换层）。
 *       布尔列一律以 0/1 整型呈现（PG 端 SELECT 显式 CAST，写参 '0'/'1' 文本双方言通用）。
 */

/**
 * @brief 列出用户某账本下的账户
 * @param include_archived 是否包含已归档账户
 * @return 行数组（每行附加 asset_count / total_value），NULL 表示查询失败
 */
csilk_json_t* account_list(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           int64_t          ledger_id,
                           bool             include_archived,
                           int64_t*         total);

/**
 * @brief 查询单条账户
 * @return 单行对象（0 行数组视为不存在）
 */
csilk_json_t* account_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 创建账户
 * @return 新 id，失败 0
 */
int64_t account_insert(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          ledger_id,
                       const char*      name,
                       const char*      institution,
                       const char*      account_no,
                       const char*      currency,
                       const char*      icon,
                       const char*      note,
                       int64_t          sort_order);

/**
 * @brief 动态更新账户基础属性（NULL 字段表示不更新）
 * @return 0: 成功, 1: 未命中/失败
 */
int account_update(csilk_db_pool_t* pool,
                   int64_t          user_id,
                   int64_t          id,
                   const char*      name,
                   const char*      institution,
                   const char*      account_no,
                   const char*      currency,
                   const char*      icon,
                   const char*      note,
                   int64_t          sort_order,
                   int              has_sort_order,
                   int              has_archived,
                   int              archived);

/**
 * @brief 硬删除账户
 * @return 0: 成功, 1: 未命中/失败
 */
int account_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 账户下资产数量
 * @return 0: 成功(出参), -1: 查询失败
 */
int account_count_assets(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          account_id,
                         int64_t*         out_count);

/**
 * @brief 账户是否挂有交易（经子资产直连或作为资金账户 linked_asset_id）
 * @return 0: 成功(*out_flag 为 0/1), -1: 查询失败
 */
int
account_has_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t account_id, int* out_flag);

/**
 * @brief 查找某账本下名称为「默认账户」的账户 id
 * @return 找到的 id；不存在 0
 */
int64_t account_find_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 幂等创建「默认账户」（已存在则返回既有 id）
 * @return 账户 id；失败 0
 */
int64_t account_create_default(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);
