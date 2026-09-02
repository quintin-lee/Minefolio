/**
 * @file tx_types.c
 * @brief 交易类型元数据注册表与资金流动方向推导实现
 *
 * 实现了静态交易类型注册表定义、基于交易类型的资金增减符号推导、
 * 以及主资产与联动账户在各种交易场景下的变动量计算。
 */

#include "tx_types.h"
#include <string.h>

/**
 * @brief 内部静态常量数组：系统支持的交易类型元数据表
 */
static const tx_type_t TX_TYPES[] = {
    {"deposit",      "存入",   "in",  "out", "in",  0, 1},
    {"withdrawal",   "取出",   "out", "in",  "out", 0, 1},
    {"buy",          "买入",   "in",  "out", "out", 1, 1},
    {"sell",         "卖出",   "out", "in",  "in",  1, 1},
    {"transfer_in",  "转入",   "in",  "out", "in",  0, 0},
    {"transfer_out", "转出",   "out", "in",  "out", 0, 0},
    {"fee",          "手续费", "out", "out", "out", 0, 1},
    {"income",       "收益",   "in",  "in",  "in",  0, 1},
    {"loss",         "亏损",   "out", "out", "out", 0, 1},
    {"interest",     "利息",   "in",  "out", "in",  0, 1},
};

/**
 * @brief 根据交易类型编码查找对应的元数据定义
 *
 * @param[in] type 交易类型编码
 * @return const tx_type_t* 命中则返回描述结构体，未命中返回 NULL
 */
const tx_type_t*
tx_type_lookup(const char* type)
{
    if (!type) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(TX_TYPES) / sizeof(TX_TYPES[0]); i++) {
        if (strcmp(TX_TYPES[i].code, type) == 0) {
            return &TX_TYPES[i];
        }
    }
    return NULL;
}

/**
 * @brief 计算主目标资产在特定交易类型下的实际余额变动量
 *
 * @param[in] type 交易类型编码
 * @param[in] amount 交易金额
 * @param[in] price 单价（未使用）
 * @param[in] qty 数量（未使用）
 * @return double 变动增量
 */
double
tx_delta(const char* type, double amount, double price, double qty)
{
    (void)price;
    (void)qty;
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) {
        return 0;
    }
    return strcmp(t->balance_dir, "in") == 0 ? amount : -amount;
}

/**
 * @brief 计算关联资产默认变动量
 *
 * @param[in] type 交易类型编码
 * @param[in] amount 交易金额
 * @return double 变动增量
 */
static double
tx_linked_delta(const char* type, double amount)
{
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) {
        return 0;
    }
    return strcmp(t->linked_dir, "in") == 0 ? amount : -amount;
}

/**
 * @brief 计算关联资金账户在特定交易下的实际余额变动量
 *
 * @param[in] type 交易类型编码
 * @param[in] amount 交易金额
 * @param[in] tdelta 主资产已计算的变动量
 * @return double 关联资产变动量
 */
double
tx_effective_ldelta(const char* type, double amount, double tdelta)
{
    if (!type) {
        return 0;
    }
    if (strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0) {
        return -tdelta;
    }
    return tx_linked_delta(type, amount);
}