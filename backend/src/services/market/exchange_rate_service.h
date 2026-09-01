#pragma once
#include "csilk/csilk.h"

/* Get conversion rate from given currency to base currency (CNY) */
double exchange_rate_get_to_cny(const char* currency);

/* Refresh all major exchange rates from Yahoo Finance */
void exchange_rate_refresh_all(void);

/* Get all rates as a JSON dictionary: {"USD": 7.15, "HKD": 0.92, ...} */
csilk_json_t* exchange_rate_list_all(void);
