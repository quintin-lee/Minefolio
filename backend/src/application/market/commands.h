#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct search_market_cmd {
    const char* keyword;
} search_market_cmd_t;

typedef struct fetch_quote_cmd {
    const char* symbol;
    const char* source;
} fetch_quote_cmd_t;

typedef struct update_market_settings_cmd {
    const char* market_proxy;
    bool        has_auto_sync;
    bool        market_auto_sync;
    bool        has_interval;
    int         market_sync_interval_min;
    const char* market_sync_mode;
} update_market_settings_cmd_t;

typedef struct update_exchange_rate_cmd {
    const char* currency;
    double      rate;
} update_exchange_rate_cmd_t;
