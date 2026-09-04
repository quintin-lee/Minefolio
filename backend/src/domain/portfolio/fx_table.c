#include "domain/portfolio/fx_table.h"
#include <stdlib.h>
#include <string.h>

void
mf_fx_rate_table_init(mf_fx_rate_table_t* table)
{
    if (!table) {
        return;
    }
    table->count = 0;
    table->capacity = 8;
    table->entries = (mf_fx_rate_entry_t*)calloc(table->capacity, sizeof(mf_fx_rate_entry_t));
}

void
mf_fx_rate_table_free(mf_fx_rate_table_t* table)
{
    if (!table) {
        return;
    }
    if (table->entries) {
        free(table->entries);
        table->entries = NULL;
    }
    table->count = 0;
    table->capacity = 0;
}

int
mf_fx_rate_table_add(mf_fx_rate_table_t* table, currency_t from, currency_t to, rate_t rate)
{
    if (!table) {
        return -1;
    }
    if (table->count >= table->capacity) {
        size_t              new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        mf_fx_rate_entry_t* new_entries =
            (mf_fx_rate_entry_t*)realloc(table->entries, new_cap * sizeof(mf_fx_rate_entry_t));
        if (!new_entries) {
            return -1;
        }
        table->entries = new_entries;
        table->capacity = new_cap;
    }
    table->entries[table->count].from_currency = from;
    table->entries[table->count].to_currency = to;
    table->entries[table->count].rate = rate;
    table->entries[table->count].is_valid = true;
    table->count++;
    return 0;
}

int
mf_fx_convert_money(money_t                   src,
                    currency_t                target_currency,
                    const mf_fx_rate_table_t* rate_table,
                    money_t*                  out_converted)
{
    if (!out_converted) {
        return -1;
    }

    // 1. Identity conversion: source currency equals target currency
    if (currency_equals(src.currency, target_currency)) {
        *out_converted = src;
        return 0;
    }

    if (!rate_table || rate_table->count == 0 || !rate_table->entries) {
        // Missing rate table: strictly fail! Do NOT do implicit 1:1 conversion.
        return -1;
    }

    // 2. Direct match from -> to
    for (size_t i = 0; i < rate_table->count; i++) {
        const mf_fx_rate_entry_t* entry = &rate_table->entries[i];
        if (!entry->is_valid) {
            continue;
        }

        if (currency_equals(entry->from_currency, src.currency) &&
            currency_equals(entry->to_currency, target_currency)) {
            if (rate_convert_money(src, entry->rate, out_converted) == DECIMAL_OK) {
                return 0;
            }
        }
    }

    // 3. Inverse match to -> from
    for (size_t i = 0; i < rate_table->count; i++) {
        const mf_fx_rate_entry_t* entry = &rate_table->entries[i];
        if (!entry->is_valid) {
            continue;
        }

        if (currency_equals(entry->from_currency, target_currency) &&
            currency_equals(entry->to_currency, src.currency)) {
            rate_t inv_rate;
            if (rate_invert(entry->rate, 6, &inv_rate) == DECIMAL_OK) {
                if (rate_convert_money(src, inv_rate, out_converted) == DECIMAL_OK) {
                    return 0;
                }
            }
        }
    }

    // 4. Rate not found: strictly reject implicit conversion
    return -1;
}
