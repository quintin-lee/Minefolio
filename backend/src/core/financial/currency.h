#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @file currency.h
 * @brief ISO 4217 货币与数字加密货币定义、标准小数精度与合法性校验
 */

#define CURRENCY_CODE_LEN 8

/**
 * @brief 货币描述结构体
 */
typedef struct {
    char    code[CURRENCY_CODE_LEN]; /**< 货币代码（如 "CNY", "USD", "EUR", "BTC"），大写归一化 */
    uint8_t precision; /**< 货币最小单位标准精度（小数位数，如 CNY=2, JPY=0, BTC=8） */
} currency_t;

/**
 * @brief 从字符串解析并归一化创建货币对象
 * @param code 货币代码字符串（支持任意大小写，如 "cny", "USD", "btc"）
 * @return currency_t 货币对象；若传入 NULL 或空串则返回 CURRENCY_NONE。
 */
currency_t currency_from_str(const char* code);

/**
 * @brief 获取货币代码字符串
 * @param cur 货币对象指针
 * @return const char* 货币大写代码字符串（非 NULL）
 */
const char* currency_code(const currency_t* cur);

/**
 * @brief 比较两个货币是否完全一致
 * @param a 货币 A
 * @param b 货币 B
 * @return true 一致；false 不一致
 */
bool currency_equals(currency_t a, currency_t b);

/**
 * @brief 校验货币代码是否有效（非 NONE 且非空）
 * @param cur 货币对象
 * @return true 有效；false 无效
 */
bool currency_is_valid(currency_t cur);

/**
 * @brief 获取该货币的标准小数精度位数
 * @param cur 货币对象
 * @return uint8_t 精度位数（如 2、0、8）
 */
uint8_t currency_precision(currency_t cur);

/* 预设常用法定货币与数字资产常量 */
extern const currency_t CURRENCY_CNY;
extern const currency_t CURRENCY_USD;
extern const currency_t CURRENCY_EUR;
extern const currency_t CURRENCY_HKD;
extern const currency_t CURRENCY_JPY;
extern const currency_t CURRENCY_GBP;
extern const currency_t CURRENCY_AUD;
extern const currency_t CURRENCY_CAD;
extern const currency_t CURRENCY_SGD;
extern const currency_t CURRENCY_BTC;
extern const currency_t CURRENCY_ETH;
extern const currency_t CURRENCY_USDT;
extern const currency_t CURRENCY_NONE;
