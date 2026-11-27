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

/**
 * @brief 插入转账关联交易记录（transfer_out），固化汇率 fx_rate_to_cny
 * @return 新交易记录 ID，失败返回 <= 0
 */
int64_t mf_transfer_repo_insert_out_transaction_fx(void*       pool,
                                                   int64_t     user_id,
                                                   int64_t     asset_id,
                                                   int64_t     linked_asset_id,
                                                   double      amount,
                                                   const char* currency,
                                                   double      fx_rate_to_cny,
                                                   const char* date,
                                                   const char* note,
                                                   int64_t     ledger_id);

/**
 * @brief 插入转账关联交易记录（transfer_in），固化汇率 fx_rate_to_cny
 * @return 新交易记录 ID，失败返回 <= 0
 */
int64_t mf_transfer_repo_insert_in_transaction_fx(void*       pool,
                                                  int64_t     user_id,
                                                  int64_t     asset_id,
                                                  int64_t     linked_asset_id,
                                                  double      amount,
                                                  const char* currency,
                                                  double      fx_rate_to_cny,
                                                  const char* date,
                                                  const char* note,
                                                  int64_t     parent_tx_id,
                                                  int64_t     ledger_id);
