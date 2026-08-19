#pragma once
#include <stdint.h>

/* Token response */
typedef struct {
    char     token[512];
    double   expires_in;
} token_resp_t;

/* User response */
typedef struct {
    int64_t  id;
    char     username[128];
    char     created_at[64];
} user_resp_t;

/* Category response */
typedef struct {
    int64_t  id;
    char     name[128];
    char     parent_name[128];
    char     type[32];
    char     asset_type[32];
    char     currency[16];
    char     icon[64];
    int64_t  parent_id;
    int      sort_order;
} category_resp_t;

/* Asset response */
typedef struct {
    int64_t  id;
    char     name[128];
    char     account_no[64];
    char     currency[16];
    char     note[256];
    char     category_name[128];
    char     asset_type[32];
    int64_t  category_id;
    double   current_value;
    double   quantity;
    double   cost_basis;
    double   net_value;
    char     created_at[64];
    char     updated_at[64];
} asset_resp_t;

/* Tag response */
typedef struct {
    int64_t  id;
    char     name[128];
    char     color[16];
    char     created_at[64];
} tag_resp_t;

/* Transaction response */
typedef struct {
    int64_t  id;
    int64_t  asset_id;
    char     transaction_type[32];
    double   amount;
    double   quantity;
    double   price_per_unit;
    char     currency[16];
    char     transaction_date[32];
    char     note[256];
    char     created_at[64];
} transaction_resp_t;
