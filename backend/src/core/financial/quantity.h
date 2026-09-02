#pragma once
#include "core/financial/decimal.h"

/**
 * @file quantity.h
 * @brief 标的资产持仓份额与数量强类型模型
 *
 * 适用于股票股数、公募基金份额、加密货币代币数量、大宗商品单位等。
 * 支持高达 8~12 位小数精度（如比特币 0.00000001 聪），与法币金额严格区分。
 */

/**
 * @brief 资产持仓份额模型
 */
typedef struct {
    decimal_t units; /**< 份额数量高精度十进制数 */
} quantity_t;

/* 构造与转换 */
quantity_t    quantity_zero(void);
quantity_t    quantity_from_decimal(decimal_t d);
decimal_err_t quantity_from_string(const char* str, quantity_t* out);
decimal_err_t quantity_from_double(double d, int32_t scale, quantity_t* out);
double        quantity_to_double(quantity_t q);
int           quantity_to_string(quantity_t q, char* buf, size_t buf_size);
int           quantity_to_string_fixed(quantity_t q, int32_t scale, char* buf, size_t buf_size);

/* 算术操作 */
decimal_err_t quantity_add(quantity_t a, quantity_t b, quantity_t* out);
decimal_err_t quantity_sub(quantity_t a, quantity_t b, quantity_t* out);
int           quantity_cmp(quantity_t a, quantity_t b);

/* 状态判定 */
bool       quantity_is_zero(quantity_t q);
bool       quantity_is_negative(quantity_t q);
bool       quantity_is_positive(quantity_t q);
quantity_t quantity_abs(quantity_t q);
quantity_t quantity_neg(quantity_t q);
