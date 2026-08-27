#pragma once
#include <stdint.h>
#include "csilk/reflection/reflect.h"

/* Token response */
typedef struct {
    char   token[512];
    double expires_in;
} token_resp_t;
#define TOKEN_RESP_MAP(_)                                                                          \
    _(token_resp_t, token, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)                 \
    _(token_resp_t, expires_in, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)
CSILK_REGISTER_REFLECT(token_resp_t, TOKEN_RESP_MAP)

/* User response */
typedef struct {
    int64_t id;
    char    username[128];
    char    created_at[64];
} user_resp_t;
#define USER_RESP_MAP(_)                                                                           \
    _(user_resp_t, id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)                       \
    _(user_resp_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)               \
    _(user_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(user_resp_t, USER_RESP_MAP)

/* Category response */
typedef struct {
    int64_t id;
    char    name[128];
    char    parent_name[128];
    char    type[32];
    char    asset_type[32];
    char    currency[16];
    char    icon[64];
    int64_t parent_id;
    int     sort_order;
} category_resp_t;
#define CATEGORY_RESP_MAP(_)                                                                       \
    _(category_resp_t, id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)                   \
    _(category_resp_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)               \
    _(category_resp_t, parent_name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)        \
    _(category_resp_t, type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)                \
    _(category_resp_t, asset_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)          \
    _(category_resp_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)            \
    _(category_resp_t, icon, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)                \
    _(category_resp_t, parent_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)            \
    _(category_resp_t, sort_order, CSILK_TYPE_INT32, sizeof(int32_t), 0, false, nullptr)
CSILK_REGISTER_REFLECT(category_resp_t, CATEGORY_RESP_MAP)

/* Asset response */
typedef struct {
    int64_t id;
    char    name[128];
    char    account_no[64];
    char    currency[16];
    char    note[256];
    char    category_name[128];
    char    asset_type[32];
    int64_t category_id;
    double  current_value;
    double  quantity;
    double  cost_basis;
    double  net_value;
    char    created_at[64];
    char    updated_at[64];
} asset_resp_t;
#define ASSET_RESP_MAP(_)                                                                          \
    _(asset_resp_t, id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)                      \
    _(asset_resp_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)                  \
    _(asset_resp_t, account_no, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)             \
    _(asset_resp_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)               \
    _(asset_resp_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)                  \
    _(asset_resp_t, category_name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)         \
    _(asset_resp_t, asset_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)             \
    _(asset_resp_t, category_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)             \
    _(asset_resp_t, current_value, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)           \
    _(asset_resp_t, quantity, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)                \
    _(asset_resp_t, cost_basis, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)              \
    _(asset_resp_t, net_value, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)               \
    _(asset_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)             \
    _(asset_resp_t, updated_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(asset_resp_t, ASSET_RESP_MAP)

/* Tag response */
typedef struct {
    int64_t id;
    char    name[128];
    char    color[16];
    char    created_at[64];
} tag_resp_t;
#define TAG_RESP_MAP(_)                                                                            \
    _(tag_resp_t, id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)                        \
    _(tag_resp_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)                    \
    _(tag_resp_t, color, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)                    \
    _(tag_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(tag_resp_t, TAG_RESP_MAP)

/* Transaction response */
typedef struct {
    int64_t id;
    int64_t asset_id;
    int64_t linked_asset_id;
    int64_t category_id;
    char    transaction_type[32];
    char    source_type[32];
    char    direction[16];
    char    linked_direction[16];
    double  amount;
    double  quantity;
    double  price_per_unit;
    double  fee;
    char    currency[16];
    char    transaction_date[32];
    char    note[256];
    char    asset_name[128];
    char    linked_asset_name[128];
    char    category_name[128];
    char    created_at[64];
} transaction_resp_t;
#define TX_RESP_MAP(_)                                                                             \
    _(transaction_resp_t, id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)                \
    _(transaction_resp_t, asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)          \
    _(transaction_resp_t, linked_asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)   \
    _(transaction_resp_t, category_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)       \
    _(transaction_resp_t, transaction_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr) \
    _(transaction_resp_t, source_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)      \
    _(transaction_resp_t, direction, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)        \
    _(transaction_resp_t, linked_direction, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr) \
    _(transaction_resp_t, amount, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)            \
    _(transaction_resp_t, quantity, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)          \
    _(transaction_resp_t, price_per_unit, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)    \
    _(transaction_resp_t, fee, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)               \
    _(transaction_resp_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)         \
    _(transaction_resp_t, transaction_date, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr) \
    _(transaction_resp_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)            \
    _(transaction_resp_t, asset_name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)      \
    _(transaction_resp_t,                                                                          \
      linked_asset_name,                                                                           \
      CSILK_TYPE_STRING,                                                                           \
      sizeof(char[128]),                                                                           \
      0,                                                                                           \
      true,                                                                                        \
      nullptr)                                                                                     \
    _(transaction_resp_t, category_name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)   \
    _(transaction_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(transaction_resp_t, TX_RESP_MAP)
