#pragma once

/**
 * @file tx_types.h
 * @brief 交易类型元数据注册表与资金流动方向推导规范（Financial Core 驱动）
 *
 * 集中管理所有支持的交易类型（买入 buy、卖出 sell、转入 transfer_in、转出 transfer_out、
 * 存入 deposit、提取 withdrawal、分红 dividend、利息 interest、手续费 fee、收益 income、亏损 loss 等）
 * 的资金流动符号、报表统计归属及关联资产联动规则。
 */

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/price.h"
#include "core/financial/quantity.h"
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
 */
const tx_type_t* tx_type_lookup(const char* type);

/**
 * @brief 计算主目标资产在特定交易类型下的实际余额变动量 (money_t 强类型)
 */
money_t tx_delta_m(const char* type, money_t amount, price_t price, quantity_t qty);

/**
 * @brief 兼容接口：计算主目标资产在特定交易类型下的余额变动量
 */
double tx_delta(const char* type, double amount, double price, double qty);

/**
 * @brief 计算关联资金账户在特定交易下的实际余额变动量 (money_t 强类型)
 */
money_t tx_effective_ldelta_m(const char* type, money_t amount, money_t tdelta);

/**
 * @brief 兼容接口：计算关联资金账户在特定交易下的实际余额变动量
 */
double tx_effective_ldelta(const char* type, double amount, double tdelta);