#pragma once
#include "csilk/csilk.h"

/* Get conversion rate from given currency to base currency (CNY) */
double exchange_rate_get_to_cny(const char* currency);

/* Convert amount between any two currencies */
double exchange_rate_convert(double amount, const char* from_currency, const char* to_currency);

/* Manually update or override a currency conversion rate to CNY */
int exchange_rate_set(const char* currency, double rate_to_cny);

/* Refresh all major exchange rates from Yahoo Finance */
void exchange_rate_refresh_all(void);

/* Get all rates as a JSON dictionary: {"USD": 7.15, "HKD": 0.92, ...} */
csilk_json_t* exchange_rate_list_all(void);

/* Get historical exchange rate curve */
csilk_json_t* exchange_rate_history_list(const char* target_currency, int days);
