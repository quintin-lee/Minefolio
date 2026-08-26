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
    {"interest",     "利息",   "in",  "out", "in",  0, 1}, /* 新增类型：验证注册表驱动 */
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

static double
tx_linked_delta(const char* type, double amount)
{
    const tx_type_t* t = tx_type_lookup(type);
    if (!t) {
        return 0;
    }
    return strcmp(t->linked_dir, "in") == 0 ? amount : -amount;
}

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