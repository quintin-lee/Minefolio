#pragma once
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"

/**
 * @file rate.h
 * @brief 外汇汇率、费率与折算转换模型
 *
 * 具有源币种与目标币种属性 (from_currency -> to_currency)。
 * 提供外汇转换、反向汇率求逆与三角套汇链式合成 (Triangular Arbitrage Rate Chain)。
 */

/**
 * @brief 汇率/转换率结构体
 */
typedef struct {
    decimal_t  factor;        /**< 汇率折算乘数因子 (1 单位 from_currency = factor 单位 to_currency) */
    currency_t from_currency; /**< 源币种 */
    currency_t to_currency;   /**< 目标币种 */
} rate_t;

/* 构造与转换 */
rate_t        rate_one(currency_t from_cur, currency_t to_cur);
rate_t        rate_from_decimal(decimal_t factor, currency_t from_cur, currency_t to_cur);
decimal_err_t rate_from_string(const char* str, currency_t from_cur, currency_t to_cur, rate_t* out);
decimal_err_t rate_from_double(double d, int32_t scale, currency_t from_cur, currency_t to_cur, rate_t* out);
double        rate_to_double(rate_t r);
int           rate_to_string(rate_t r, char* buf, size_t buf_size);
int           rate_to_string_fixed(rate_t r, int32_t scale, char* buf, size_t buf_size);

/* 外汇与比率转换操作 */

/**
 * @brief 使用汇率将源币种金额转换为目标币种金额 (Money * Rate = Converted Money)
 * @param in 源币种金额
 * @param r 汇率（要求 in.currency == r.from_currency）
 * @param out 输出目标币种金额（币种设为 r.to_currency）
 * @return decimal_err_t 状态码
 */
decimal_err_t rate_convert_money(money_t in, rate_t r, money_t* out);

/**
 * @brief 计算反向汇率 (1 / Rate)
 * @param r 原始汇率 (A -> B)
 * @param scale 反向汇率保留小数位数（默认 6 位）
 * @param out 输出反向汇率 (B -> A)
 * @return decimal_err_t 状态码
 */
decimal_err_t rate_invert(rate_t r, int32_t scale, rate_t* out);

/**
 * @brief 三角套汇链式合成 (A->B 结合 B->C 合成 A->C)
 * @param a_to_b 汇率 A -> B
 * @param b_to_c 汇率 B -> C
 * @param scale 合成汇率精度（默认 6 位）
 * @param out_a_to_c 输出汇率 A -> C
 * @return decimal_err_t 状态码
 */
decimal_err_t rate_chain(rate_t a_to_b, rate_t b_to_c, int32_t scale, rate_t* out_a_to_c);
