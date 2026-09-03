#pragma once

#include <stddef.h>
#include "domain/transaction/entity.h"

/**
 * @brief 校验交易实体的领域业务规则与完整性不变式
 * @param tx 待校验的交易实体
 * @param err_buf 错误提示信息缓冲区
 * @param err_len 缓冲区长度
 * @return 0: 校验通过, 非 0: 违反领域业务规则
 */
int mf_tx_rule_validate(const mf_transaction_t* tx, char* err_buf, size_t err_len);

/**
 * @brief 根据主交易实体构建其从属的手续费子单实体 (Fee Child Invariant)
 * @param parent 包含非零 fee 的主交易实体 (须已获得分配的主交易 ID)
 * @param out_fee 输出构建的手续费子单实体
 * @return 0: 成功, 非 0: 无法构建（如手续费为零或参数无效）
 */
int mf_tx_rule_build_fee_child(const mf_transaction_t* parent, mf_transaction_t* out_fee);
