#pragma once
#include <stdint.h>

typedef struct {
    int64_t  id;
    int64_t  user_id;
    int64_t  from_asset_id;
    int64_t  to_asset_id;
    double   amount;
    char     currency[16];
    char     transfer_date[32];
    char     note[256];
    char     created_at[64];
} transfer_t;
