#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char   symbol[64];
    char   name[128];
    char   source[32];
    double current_price;
    double change_percent;
    char   currency[16];
    char   quote_time[32];
} market_quote_t;

typedef struct {
    char   symbol[64];
    char   name[128];
    char   source[32];
    char   market_desc[64];
    double current_price;
    char   currency[16];
} market_search_item_t;

typedef struct {
    char market_proxy[256];
    bool market_auto_sync;
    int  market_sync_interval_min;
} market_settings_t;
