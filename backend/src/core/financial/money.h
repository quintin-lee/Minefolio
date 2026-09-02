#pragma once
#include "core/financial/currency.h"
#include "core/financial/decimal.h"

/**
 * @file money.h
 * @brief 货币金额领域强类型模型
 *
 * 绑定特定法定或加密货币 (currency_t) 的高精度金额模型。
 * 强制执行跨币种算术安全性校验（杜绝跨币种直接相加减的金融漏洞）。
 */

/**
 * @brief 货币金额结构体
 */
typedef struct {
    decimal_t  amount;   /**< 高精度金额数值 */
    currency_t currency; /**< 绑定币种 */
} money_t;

/* 构造与转换 */
money_t       money_zero(currency_t cur);
money_t       money_from_decimal(decimal_t amt, currency_t cur);
decimal_err_t money_from_string(const char* str, currency_t cur, money_t* out);
decimal_err_t money_from_int(int64_t val, currency_t cur, money_t* out);
decimal_err_t money_from_double(double d, currency_t cur, money_t* out);
double        money_to_double(money_t m);
int           money_to_string(money_t m, char* buf, size_t buf_size);

/* 安全算术操作（异币种直接相加减返回 DECIMAL_ERR_INVALID_ARG / 异币种错误） */
decimal_err_t money_add(money_t a, money_t b, money_t* out);
decimal_err_t money_sub(money_t a, money_t b, money_t* out);
int           money_cmp(money_t a, money_t b);

/* 一元操作与判定 */
money_t       money_abs(money_t m);
money_t       money_neg(money_t m);
bool          money_is_zero(money_t m);
bool          money_is_negative(money_t m);
bool          money_is_positive(money_t m);
decimal_err_t money_round(money_t m, round_mode_t mode, money_t* out);
