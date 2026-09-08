/**
 * @file repository.h
 * @brief 转账领域仓储契约接口 (Domain Transfer Repository Contract)
 */

#pragma once

#include "domain/transfer/entity.h"
#include <stdint.h>

/**
 * @brief 校验转出与转入资产是否有效且归属用户
 * @return 1: 有效, 0: 无效
 */
int mf_transfer_repo_asset_check(void* pool, int64_t user_id, int64_t from_id, int64_t to_id);

/**
 * @brief 插入转账流水记录
 * @return 新记录 ID，失败返回 0
 */
int64_t mf_transfer_repo_insert(void* pool, int64_t user_id, const mf_transfer_t* transfer);

/**
 * @brief 插入转账关联交易记录（transfer_out）
 * @return 0: 成功, -1: 失败
 */
int mf_transfer_repo_insert_out_transaction(void*       pool,
                                            int64_t     user_id,
                                            int64_t     asset_id,
                                            double      amount,
                                            const char* currency,
                                            const char* date,
                                            const char* note);

/**
 * @brief 插入转账关联交易记录（transfer_in）
 * @return 0: 成功, -1: 失败
 */
int mf_transfer_repo_insert_in_transaction(void*       pool,
                                           int64_t     user_id,
                                           int64_t     asset_id,
                                           double      amount,
                                           const char* currency,
                                           const char* date,
                                           const char* note);
