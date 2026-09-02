#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @file decimal.h
 * @brief 128 位定点高精度十进制数运算引擎
 *
 * 彻底消除 IEEE 754 浮点数 (double/float) 引入的二进制舍入误差与精度损失。
 * 支持 0-18 位动态 scale，提供标准的加、减、乘、除、多种舍入模式、字符串格式化与反序列化。
 */

#define DECIMAL_MAX_SCALE 18

/**
 * @brief 定点数运算错误码定义
 */
typedef enum {
    DECIMAL_OK = 0,                  /**< 成功 */
    DECIMAL_ERR_OVERFLOW = -1,       /**< 算术溢出 */
    DECIMAL_ERR_DIV_BY_ZERO = -2,    /**< 除以零错误 */
    DECIMAL_ERR_INVALID_ARG = -3,    /**< 参数非法（如 scale 超出范围或空指针） */
    DECIMAL_ERR_PRECISION_LOSS = -4, /**< 精度损失截断 */
    DECIMAL_ERR_PARSE = -5           /**< 字符串解析格式错误 */
} decimal_err_t;

/**
 * @brief 舍入模式定义
 */
typedef enum {
    ROUND_HALF_UP = 0,   /**< 四舍五入 (Standard accounting, >= 0.5 远离零向上进位) */
    ROUND_HALF_EVEN = 1, /**< 银行家舍入法 (Round to nearest even, 逢五向最近偶数舍入) */
    ROUND_DOWN = 2,      /**< 向零截断 (Truncate, 直接丢弃小数部分) */
    ROUND_UP = 3,        /**< 远离零进位 (Ceil magnitude) */
    ROUND_CEIL = 4,      /**< 向正无穷方向进位 (+Inf) */
    ROUND_FLOOR = 5      /**< 向负无穷方向进位 (-Inf) */
} round_mode_t;

/**
 * @brief 128 位定点数十进制模型
 *
 * 真实数值 = mantissa * 10^(-scale)
 * 例如 19.99 表示为 mantissa=1999, scale=2
 */
typedef struct {
    __int128_t mantissa; /**< 128 位带符号整数尾数 */
    int32_t    scale;    /**< 小数位数 (0 <= scale <= 18) */
} decimal_t;

/* 构造与常量 */
decimal_t     decimal_zero(void);
decimal_t     decimal_one(void);
decimal_t     decimal_from_int(int64_t val);
decimal_t     decimal_from_parts(int64_t integer_part, int64_t fractional_part, int32_t scale);
decimal_err_t decimal_from_string(const char* str, decimal_t* out);
decimal_err_t decimal_from_double(double d, int32_t scale, decimal_t* out);
double        decimal_to_double(decimal_t d);
int           decimal_to_string(decimal_t d, char* buf, size_t buf_size);
int decimal_to_string_fixed(decimal_t d, int32_t target_scale, char* buf, size_t buf_size);

/* 算术基础运算 */
decimal_err_t decimal_add(decimal_t a, decimal_t b, decimal_t* out);
decimal_err_t decimal_sub(decimal_t a, decimal_t b, decimal_t* out);
decimal_err_t decimal_mul(decimal_t a, decimal_t b, decimal_t* out);
decimal_err_t
decimal_div(decimal_t a, decimal_t b, int32_t target_scale, round_mode_t mode, decimal_t* out);

/* 一元与比对运算 */
decimal_t     decimal_abs(decimal_t d);
decimal_t     decimal_neg(decimal_t d);
int           decimal_cmp(decimal_t a, decimal_t b);
bool          decimal_is_zero(decimal_t d);
bool          decimal_is_negative(decimal_t d);
bool          decimal_is_positive(decimal_t d);
decimal_err_t decimal_round(decimal_t d, int32_t target_scale, round_mode_t mode, decimal_t* out);
decimal_t     decimal_rescale(decimal_t d, int32_t target_scale);
decimal_t     decimal_min(decimal_t a, decimal_t b);
decimal_t     decimal_max(decimal_t a, decimal_t b);
