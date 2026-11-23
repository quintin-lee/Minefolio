/**
 * @file transfer_repo_impl.h
 * @brief 转账仓储 SQL 实现声明 (Infrastructure Transfer Repository)
 */

#pragma once

#include "domain/transfer/repository.h"
#include <stdint.h>

/**
 * @brief 清理 transfers 辅助流水（优先按 tx_id 精准删除，兜底按对等属性删除历史遗留行）
 */
int transfer_repo_delete_paired(void*   pool,
                                int64_t user_id,
                                int64_t tx_id,
                                int64_t from_asset_id,
                                int64_t to_asset_id,
                                double  amount);
#include <stdint.h>

/**
 * @brief 清理 transfers 辅助流水（优先按 tx_id 精准删除，兜底按对等属性删除历史遗留行）
 */
int transfer_repo_delete_paired(void*   pool,
                                int64_t user_id,
                                int64_t tx_id,
                                int64_t from_asset_id,
                                int64_t to_asset_id,
                                double  amount);
