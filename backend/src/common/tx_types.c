#include "tx_types.h"
#include <string.h>

static const tx_type_t TX_TYPES[] = {
    { "deposit",      "存入",   "in",  "out", "in",  0, 1 },
    { "withdrawal",   "取出",   "out", "in",  "out", 0, 1 },
    { "buy",          "买入",   "in",  "out", "out", 1, 1 },
    { "sell",         "卖出",   "out", "in",  "in",  1, 1 },
    { "transfer_in",  "转入",   "in",  "out", "in",  0, 0 },
    { "transfer_out", "转出",   "out", "in",  "out", 0, 0 },
    { "fee",          "手续费", "out", "out", "out", 0, 1 },
    { "income",       "收益",   "in",  "in",  "in",  0, 1 },
    { "loss",         "亏损",   "out", "out", "out", 0, 1 },
    { "interest",     "利息",   "in",  "out", "in",  0, 1 },  /* 新增类型：验证注册表驱动 */
};

const tx_type_t* tx_type_lookup(const char* type) {
    if (!type) return NULL;
    for (size_t i = 0; i < sizeof(TX_TYPES) / sizeof(TX_TYPES[0]); i++) {
        if (strcmp(TX_TYPES[i].code, type) == 0) return &TX_TYPES[i];
    }
    return NULL;
}