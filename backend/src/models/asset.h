#pragma once
#include <stdint.h>

typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t category_id;
    char    name[128];
    char    account_no[64];
    double  current_value;
    char    currency[16];
    char    note[256];
    double  quantity;
    double  cost_basis;
    double  net_value;
    char    created_at[64];
    char    updated_at[64];
} asset_t;
