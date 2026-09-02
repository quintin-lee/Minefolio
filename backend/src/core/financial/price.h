#pragma once
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/quantity.h"

/**
 * @file price.h
 * @brief 标的资产单位报价与成交单价模型
 *
 * 具有计价币种属性 (Quote Currency)。
 * 提供量价量纲转换：
 * - 价格 × 数量 = 交易金额 (Price * Quantity = Money)
 * - 交易金额 ÷ 数量 = 成交单价 (Money / Quantity = Price)
 * - 交易金额 ÷ 单价 = 买入数量 (Money / Price = Quantity)
 */

/**
 * @brief 资产单价结构体
 */
typedef struct {
    decimal_t  unit_price; /**< 单位单价十进制数值 */
    currency_t currency;   /**< 报价计价币种 (Quote Currency) */
} price_t;

/* 构造与转换 */
price_t       price_zero(currency_t cur);
price_t       price_from_decimal(decimal_t d, currency_t cur);
decimal_err_t price_from_string(const char* str, currency_t cur, price_t* out);
decimal_err_t price_from_double(double d, int32_t scale, currency_t cur, price_t* out);
double        price_to_double(price_t p);
int           price_to_string(price_t p, char* buf, size_t buf_size);
int           price_to_string_fixed(price_t p, int32_t scale, char* buf, size_t buf_size);
int           price_cmp(price_t a, price_t b);

/* 量价转换运算（量纲分析保证） */

/**
 * @brief 单价 × 数量 = 货币金额 (Price * Quantity = Money)
 * @param p 单价
 * @param q 数量
 * @param out 输出金额（继承单价的报价币种）
 * @return decimal_err_t 状态码
 */
decimal_err_t price_times_quantity(price_t p, quantity_t q, money_t* out);

/**
 * @brief 货币金额 ÷ 数量 = 成交单价 (Money / Quantity = Price)
 * @param m 总金额
 * @param q 份额数量
 * @param scale 单价保留小数位（默认通常为 4~6 位）
 * @param mode 舍入模式
 * @param out 输出单价
 * @return decimal_err_t 状态码
 */
decimal_err_t money_div_quantity(money_t m, quantity_t q, int32_t scale, round_mode_t mode, price_t* out);

/**
 * @brief 货币金额 ÷ 单价 = 成交份额数量 (Money / Price = Quantity)
 * @param m 投资预算金额
 * @param p 标的单价
 * @param scale 份额保留小数位（通常 4~8 位）
 * @param mode 舍入模式
 * @param out 输出份额数量
 * @return decimal_err_t 状态码
 */
decimal_err_t money_div_price(money_t m, price_t p, int32_t scale, round_mode_t mode, quantity_t* out);
