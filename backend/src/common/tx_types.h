#pragma once

/**
 * @file tx_types.h
 * @brief 交易类型元数据注册表与资金流动方向推导规范
 *
 * 集中管理所有交易类型（买入 buy、卖出 sell、转入 transfer_in、转出 transfer_out、
 * 存入 deposit、提取 withdraw、分红 dividend、利息 interest、手续费 fee 等）
 * 的资金流动符号、报表统计归属及关联资产联动规则。
 */

/**
 * @brief 交易类型元数据描述结构体
 */
typedef struct {
    const char* code;        /**< 交易类型唯一编码（数据库存储值，如 "buy", "sell"） */
    const char* label;       /**< 中文显示标签（用于 UI/日志展示，如 "买入", "卖出"） */
    const char* balance_dir; /**< 目标主资产的余额流动方向："in" 为增加 (+)，"out" 为扣减 (-) */
    const char* linked_dir;  /**< 关联资金账户 (funding asset) 的余额流动方向："in" 或 "out" */
    const char* stat_dir;    /**< 现金流统计口径方向："in" (现金流入) 或 "out" (现金流出) */
    int         is_trading;  /**< 是否属于标的买卖交易（1: 是，显示单价与数量；0: 普通资金变动） */
    int in_performance;      /**< 是否纳入投资盈亏与回报率分析统计（1: 纳入；0: 排除，如纯转账） */
} tx_type_t;

/**
 * @brief 根据交易类型编码查找对应的元数据定义。
 *
 * @param type 交易类型字符串（如 "buy", "sell", "transfer" 等）
 * @return const tx_type_t* 命中则返回类型描述结构体指针；若未定义则返回 NULL。
 */
const tx_type_t* tx_type_lookup(const char* type);

/**
 * @brief 计算主目标资产在特定交易类型下的实际余额变动量 (Delta)。
 *
 * 规则：balance_dir=="in" 时返回 +amount，"out" 时返回 -amount。
 *
 * @param type   交易类型编码
 * @param amount 交易发生额
 * @param price  交易单价（用于辅助核算）
 * @param qty    交易数量/份额
 * @return double 目标资产余额变动量（带正负符号）
 */
double tx_delta(const char* type, double amount, double price, double qty);

/**
 * @brief 计算关联资金账户 (Linked Funding Asset) 在特定交易下的实际余额变动量。
 *
 * @param type   交易类型编码
 * @param amount 交易发生额
 * @param tdelta 主资产变动量 (tx_delta 计算结果)
 * @return double 资金账户应扣除或增加的变动量（例如买入资产时，资金账户扣减对应金额）
 */
double tx_effective_ldelta(const char* type, double amount, double tdelta);