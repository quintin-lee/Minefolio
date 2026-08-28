#pragma once
#include "common/market_types.h"
#include <stddef.h>

/* HTTP helper using libcurl */
char* quote_engine_http_get(const char* url, const char* proxy, int timeout_sec, size_t* out_len);

/* Global proxy configuration */
void        quote_engine_set_proxy(const char* proxy);
const char* quote_engine_get_proxy(void);

/* Search across all market drivers */
int quote_engine_search(const char* keyword, market_search_item_t* out_items, int max_items);

/* Fetch quote for a single symbol */
int
quote_engine_fetch_quote(const char* symbol, const char* source_type, market_quote_t* out_quote);

/* Test connectivity for all drivers */
int
quote_engine_test_connection(const char* proxy, char* out_msg, size_t msg_cap, int* out_latency_ms);
