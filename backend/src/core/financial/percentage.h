#pragma once
#include "core/financial/decimal.h"
#include "core/financial/money.h"

/**
 * @file percentage.h
 * @brief 百分比率、收益率与权重占比模型
 *
 * 表示百分数（如 15.5 表示 15.5%）。
 * 提供百分比应用（Money * (Pct / 100) = Money）与比率计算（(Part / Whole) * 100 = Pct）。
 */

/**
 * @brief 百分比结构体
 */
typedef struct {
    decimal_t percent; /**< 百分比数值，如 15.50 表示 15.50% */
} percentage_t;

/* 构造与转换 */
percentage_t  percentage_zero(void);
percentage_t  percentage_from_decimal(decimal_t pct);
decimal_err_t percentage_from_string(const char* str, percentage_t* out);
decimal_err_t percentage_from_double(double d, int32_t scale, percentage_t* out);
double        percentage_to_double(percentage_t p);
int           percentage_to_string(percentage_t p, char* buf, size_t buf_size);
int           percentage_to_string_fixed(percentage_t p, int32_t scale, char* buf, size_t buf_size);
int           percentage_cmp(percentage_t a, percentage_t b);

/* 运算操作 */

/**
 * @brief 将百分比应用到货币金额上：Out = In * (Percent / 100)
 * @param in 输入金额
 * @param p 百分比
 * @param out 输出金额
 * @return decimal_err_t 状态码
 */
decimal_err_t percentage_apply(money_t in, percentage_t p, money_t* out);

/**
 * @brief 计算部分在整体中的百分占比：Pct = (Part / Whole) * 100
 * @param part 局部金额
 * @param whole 总体金额
 * @param scale 百分比小数位（默认 2~4 位）
 * @param mode 舍入模式
 * @param out 输出百分比
 * @return decimal_err_t 状态码
 */
decimal_err_t
percentage_calc(money_t part, money_t whole, int32_t scale, round_mode_t mode, percentage_t* out);
