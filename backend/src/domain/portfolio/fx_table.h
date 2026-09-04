#pragma once

#include "core/financial/currency.h"
#include "core/financial/money.h"
#include "core/financial/rate.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    currency_t from_currency;
    currency_t to_currency;
    rate_t     rate; /**< 汇率模型: target = src * rate */
    bool       is_valid;
} mf_fx_rate_entry_t;

typedef struct {
    size_t              count;
    size_t              capacity;
    mf_fx_rate_entry_t* entries;
} mf_fx_rate_table_t;

/**
 * @brief 初始化汇率表
 */
void mf_fx_rate_table_init(mf_fx_rate_table_t* table);

/**
 * @brief 释放汇率表资源
 */
void mf_fx_rate_table_free(mf_fx_rate_table_t* table);

/**
 * @brief 注册一条汇率记录到汇率表
 */
int mf_fx_rate_table_add(mf_fx_rate_table_t* table, currency_t from, currency_t to, rate_t rate);

/**
 * @brief 显式外汇金额换算 (Explicit FX Conversion)
 * @param src 源币种金额
 * @param target_currency 目标计价币种
 * @param rate_table 汇率表指针
 * @param[out] out_converted 输出换算后的目标币种金额
 * @return 0 成功, -1 失败 (若币种不同且汇率表中无有效汇率，严禁隐式 1:1 折算，严格报错拦截)
 */
int mf_fx_convert_money(money_t                   src,
                        currency_t                target_currency,
                        const mf_fx_rate_table_t* rate_table,
                        money_t*                  out_converted);
