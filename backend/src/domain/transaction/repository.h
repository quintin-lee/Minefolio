#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/transaction/entity.h"

/**
 * @brief 交易仓储抽象契约接口 (Domain Repository Contract)
 * @note 纯 C 契约，入参出参仅允许领域实体与标量数值，严禁返回 JSON 节点或编写 SQL
 */

/**
 * @brief 根据交易 ID 查询单条交易
 * @return 0: 成功查到, 1: 不存在, -1: 数据库错误
 */
int mf_tx_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_transaction_t* out_tx);

/**
 * @brief 持久化保存新交易
 * @return 0: 成功, -1: 失败
 */
int mf_tx_repo_save(void* db_pool, const mf_transaction_t* tx, int64_t* out_id);

/**
 * @brief 更新现有交易信息
 * @return 0: 成功, -1: 失败
 */
int mf_tx_repo_update(void* db_pool, const mf_transaction_t* tx);

/**
 * @brief 删除单条主交易
 * @return 0: 成功, -1: 失败
 */
int mf_tx_repo_delete(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 查询指定父交易名下的全部手续费子单 (Fee Child Rows)
 * @param out_list 动态分配的实体数组，需调用 mf_tx_repo_free_list 释放
 * @param out_count 查询到的子单数量
 * @return 0: 成功, -1: 失败
 */
int mf_tx_repo_find_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id,
                                 mf_transaction_t** out_list, size_t* out_count);

/**
 * @brief 级联物理删除指定主单名下的全部手续费子单
 * @return 0: 成功, -1: 失败
 */
int mf_tx_repo_delete_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id);

/**
 * @brief 释放由仓储分配的实体数组内存
 */
void mf_tx_repo_free_list(mf_transaction_t* list, size_t count);
