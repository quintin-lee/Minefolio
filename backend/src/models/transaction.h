#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  asset_id;
    int64_t  linked_asset_id;
    int64_t  category_id;
    char     transaction_type[32];
    char     source_type[32];
    int      direction;
    int      linked_direction;
    double   amount;
    double   price_per_unit;
    double   quantity;
    char     currency[16];
    char     transaction_date[32];
    char     note[256];
    char     created_at[64];
} transaction_t;
