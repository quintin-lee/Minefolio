#pragma once

typedef struct {
    const char* code;           /* transaction_type value */
    const char* label;          /* 中文标签（展示/前端） */
    const char* balance_dir;    /* 目标资产余额方向: in=+amount, out=-amount */
    const char* linked_dir;     /* 关联资金账户余额方向 */
    const char* stat_dir;       /* 现金流统计/展示方向: in/out（写入 direction 列） */
    int         is_trading;     /* buy/sell：才显示单价×数量 */
    int         in_performance; /* 是否计入交易盈亏报表（语义上排除转账） */
} tx_type_t;

const tx_type_t* tx_type_lookup(const char* type);

/** @brief 目标资产余额增减: balance_dir=="in" → +amount, else -amount. */
double tx_delta(const char* type, double amount, double price, double qty);

/** @brief 关联资金账户余额增减，apply transfer semantics (transfer → -tdelta). */
double tx_effective_ldelta(const char* type, double amount, double tdelta);