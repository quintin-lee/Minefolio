/**
 * @file tx_types.c
 * @brief 交易类型元数据注册表与资金流动方向推导实现（Financial Core 驱动）
 */

#include "tx_types.h"
#include <string.h>

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

money_t
tx_delta_m(const char* type, money_t amount, price_t price, quantity_t qty)
{
    (void)price;
    (void)qty;
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) {
        return money_zero(amount.currency);
    }
    return strcmp(t->balance_dir, "in") == 0 ? amount : money_neg(amount);
}

double
tx_delta(const char* type, double amount, double price, double qty)
{
    money_t    m_amt;
    price_t    p;
    quantity_t q;
    money_from_double(amount, CURRENCY_CNY, &m_amt);
    price_from_double(price, 4, CURRENCY_CNY, &p);
    quantity_from_double(qty, 4, &q);

    money_t res = tx_delta_m(type, m_amt, p, q);
    return money_to_double(res);
}

static money_t
tx_linked_delta_m(const char* type, money_t amount)
{
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) {
        return money_zero(amount.currency);
    }
    return strcmp(t->linked_dir, "in") == 0 ? amount : money_neg(amount);
}

money_t
tx_effective_ldelta_m(const char* type, money_t amount, money_t tdelta)
{
    if (!type) {
        return money_zero(amount.currency);
    }
    if (strcmp(type, "transfer_in") == 0 || strcmp(type, "transfer_out") == 0) {
        return money_neg(tdelta);
    }
    return tx_linked_delta_m(type, amount);
}

double
tx_effective_ldelta(const char* type, double amount, double tdelta)
{
    money_t m_amt, m_tdelta;
    money_from_double(amount, CURRENCY_CNY, &m_amt);
    money_from_double(tdelta, CURRENCY_CNY, &m_tdelta);

    money_t res = tx_effective_ldelta_m(type, m_amt, m_tdelta);
    return money_to_double(res);
}