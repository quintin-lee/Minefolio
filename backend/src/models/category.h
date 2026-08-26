#pragma once
#include <stdint.h>

typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t parent_id;
    char    name[128];
    char    type[32];
    char    asset_type[32];
    char    currency[16];
    char    icon[64];
    int     sort_order;
    char    created_at[64];
    char    updated_at[64];
} category_t;
