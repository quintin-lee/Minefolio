#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 账户实体（纯容器：不记余额，账户小计 = 子资产 SUM(current_value)）
 * @note 每个 (user, ledger) 至少有一个「默认账户」收纳未显式挂账户的资产。
 */
typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t ledger_id;
    char    name[128];
    char
        institution[32]; /**< bank_card / platform / broker / fund_platform / cash_wallet / other */
    char    account_no[64];
    char    currency[16];
    char    icon[64];
    char    note[256];
    int64_t sort_order;
    bool    archived;
} mf_account_t;
