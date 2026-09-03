#pragma once

#include "application/transaction/commands.h"
#include "application/transaction/dtos.h"

/**
 * @brief 创建交易用例 (Create Transaction Use Case)
 */
int tx_usecase_create(void* pool, const create_tx_cmd_t* cmd, tx_usecase_result_t* out_res);

/**
 * @brief 更新交易用例 (Update Transaction Use Case)
 */
int tx_usecase_update(void* pool, const update_tx_cmd_t* cmd, tx_usecase_result_t* out_res);

/**
 * @brief 删除交易用例 (Delete Transaction Use Case)
 */
int tx_usecase_delete(void* pool, const delete_tx_cmd_t* cmd, tx_usecase_result_t* out_res);

/**
 * @brief 分页查询交易列表用例 (Query Transactions Use Case)
 */
int tx_usecase_query(void* pool, const query_tx_filter_t* filter, tx_usecase_result_t* out_res);
