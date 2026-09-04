#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/financial/currency.h"
#include "core/financial/money.h"

/**
 * @brief 资金/资产账户实体 (Funding/Custody Account Entity)
 * 管理现金/负债余额与资产托管关系
 */
typedef struct {
    int64_t    id;               /**< 账户 ID */
    int64_t    user_id;          /**< 所属用户 */
    int64_t    ledger_id;        /**< 所属账本空间 */
    char       name[128];        /**< 账户名称 (如 "招行活期") */
    char       account_no[64];   /**< 账号/卡号 */
    char       account_type[32]; /**< "cash", "bank", "credit_card", "broker", "loan" */
    currency_t currency;         /**< 账户法定货币 */
    money_t    balance;          /**< 可用现金余额（或负债额） */
    bool       is_liability;     /**< 是否为负债账户 */
    char       note[256];
} mf_account_t;
