#pragma once

/**
 * @file tx_types.h
 * @brief 交易类型元数据注册表与资金流动方向推导规范
 *
 * 集中管理所有支持的交易类型（买入 buy、卖出 sell、转入 transfer_in、转出 transfer_out、
 * 存入 deposit、提取 withdrawal、分红 dividend、利息 interest、手续费 fee、收益 income、亏损 loss 等）
 * 的资金流动符号、报表统计归属及关联资产联动规则。
 */

#include <stddef.h>

/**
 * @struct tx_type_t
 * @brief 交易类型元数据描述结构体
 */
typedef struct {
    const char* code;  /**< 交易类型唯一编码（数据库存储值，如 "buy", "sell", "transfer_in"） */
    const char* label; /**< 中文显示标签（用于 UI/日志展示，如 "买入", "卖出"） */
    const char* balance_dir; /**< 目标主资产的余额流动方向："in" 为增加 (+)，"out" 为扣减 (-) */
    const char*
        linked_dir; /**< 关联资金账户 (funding asset) 的余额流动方向："in" 为增加 (+)，"out" 为扣减 (-) */
    const char* stat_dir;   /**< 现金流统计口径方向："in" (现金流入) 或 "out" (现金流出) */
    int         is_trading; /**< 是否属于标的买卖交易（1: 是，需记录单价与数量；0: 普通资金变动） */
    int in_performance;     /**< 是否纳入投资盈亏与回报率分析统计（1: 纳入；0: 排除，如纯转账） */
} tx_type_t;

/**
 * @brief 根据交易类型编码查找对应的元数据定义
 *
 * 在内置的静态交易类型表中线性查找匹配项。
 *
 * @param[in] type 交易类型字符串（如 "buy", "sell", "transfer_in" 等），若为 NULL 则返回 NULL
 *
 * @return const tx_type_t* 命中则返回指向内部静态结构体的只读指针；若未定义则返回 NULL
 *
 * @note 内存所有权：返回内部静态结构体指针，调用方严禁修改或释放。
 * @note 线程安全性：只读常量数组查找，线程安全。
 */
const tx_type_t* tx_type_lookup(const char* type);

/**
 * @brief 计算主目标资产在特定交易类型下的实际余额变动量 (Delta)
 *
 * 规则：若类型定义中 balance_dir 为 "in"，返回 +amount；若为 "out"，返回 -amount；未识别类型返回 0。
 *
 * @param[in] type 交易类型编码
 * @param[in] amount 交易发生总额
 * @param[in] price 交易单价（辅助核算参数）
 * @param[in] qty 交易数量/份额（辅助核算参数）
 *
 * @return double 目标资产余额变动量（带正负符号）
 *
 * @note 线程安全性：线程安全。
 */
double tx_delta(const char* type, double amount, double price, double qty);

/**
 * @brief 计算关联资金账户 (Linked Funding Asset) 在特定交易下的实际余额变动量
 *
 * 根据交易类型定义及主资产变动量，计算关联支付/结算账户应变动的资金量。
 * 针对内部转账 (transfer_in / transfer_out)，联动变动量严格为 -tdelta。
 *
 * @param[in] type 交易类型编码
 * @param[in] amount 交易发生金额
 * @param[in] tdelta 主资产的实际变动量 (tx_delta 计算结果)
 *
 * @return double 资金账户应增加或扣减的变动量（例如买入资产时，资金账户扣减对应金额）
 *
 * @note 线程安全性：线程安全。
 */
double tx_effective_ldelta(const char* type, double amount, double tdelta);