#pragma once
#include "common/market_types.h"

typedef struct quote_driver_s quote_driver_t;

struct quote_driver_s {
    const char* name;
    const char* source_type;
    int (*search)(const char* keyword, market_search_item_t* out_items, int max_items);
    int (*fetch_single)(const char* symbol, market_quote_t* out_quote);
    int (*fetch_batch)(const char** symbols, int count, market_quote_t* out_quotes, int* out_count);
};

quote_driver_t* get_eastmoney_driver(void);
quote_driver_t* get_tencent_driver(void);
quote_driver_t* get_crypto_driver(void);
