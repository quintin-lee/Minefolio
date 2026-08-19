#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  category_id;
    int64_t  asset_id;
    char     expense_type[16];
    double   amount;
    char     currency[16];
    char     expense_date[32];
    char     note[256];
    char     created_at[64];
    char     updated_at[64];
} daily_expense_t;
