#pragma once
#include <stdint.h>
#include "csilk/reflection/reflect.h"

/* System setup request */
typedef struct {
    char username[128];
    char password_enc[512];
    char db_driver[64];
    char db_dsn[512];
} setup_req_t;
#define SETUP_REQ_MAP(_)                                                                           \
    _(setup_req_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)               \
    _(setup_req_t, password_enc, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)           \
    _(setup_req_t, db_driver, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)               \
    _(setup_req_t, db_dsn, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(setup_req_t, SETUP_REQ_MAP)

/* Auth requests */
typedef struct {
    char username[128];
    char password[128];
} register_req_t;
#define REGISTER_REQ_MAP(_)                                                                        \
    _(register_req_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)            \
    _(register_req_t, password, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(register_req_t, REGISTER_REQ_MAP)

typedef struct {
    char username[128];
    char password_enc[512];
} login_req_t;
#define LOGIN_REQ_MAP(_)                                                                           \
    _(login_req_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)               \
    _(login_req_t, password_enc, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(login_req_t, LOGIN_REQ_MAP)

typedef struct {
    char old_password_enc[512];
    char new_password_enc[512];
} change_pwd_req_t;
#define CHG_PWD_REQ_MAP(_)                                                                         \
    _(change_pwd_req_t, old_password_enc, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)  \
    _(change_pwd_req_t, new_password_enc, CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(change_pwd_req_t, CHG_PWD_REQ_MAP)

/* Category request */
typedef struct {
    char    name[128];
    char    type[32];
    char    asset_type[32];
    char    currency[16];
    char    icon[64];
    int64_t parent_id;
    int     sort_order;
} category_req_t;
#define CATEGORY_REQ_MAP(_)                                                                        \
    _(category_req_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)                \
    _(category_req_t, type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)                 \
    _(category_req_t, asset_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)           \
    _(category_req_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)             \
    _(category_req_t, icon, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)                 \
    _(category_req_t, parent_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)             \
    _(category_req_t, sort_order, CSILK_TYPE_INT32, sizeof(int32_t), 0, false, nullptr)
CSILK_REGISTER_REFLECT(category_req_t, CATEGORY_REQ_MAP)

/* Asset request */
typedef struct {
    char    name[128];
    char    account_no[64];
    char    currency[16];
    char    note[256];
    int64_t category_id;
    double  current_value;
    double  quantity;
    double  cost_basis;
    double  net_value;
} asset_req_t;
#define ASSET_REQ_MAP(_)                                                                           \
    _(asset_req_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)                   \
    _(asset_req_t, account_no, CSILK_TYPE_STRING, sizeof(char[64]), 0, true, nullptr)              \
    _(asset_req_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)                \
    _(asset_req_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)                   \
    _(asset_req_t, category_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)              \
    _(asset_req_t, current_value, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)            \
    _(asset_req_t, quantity, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)                 \
    _(asset_req_t, cost_basis, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)               \
    _(asset_req_t, net_value, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)
CSILK_REGISTER_REFLECT(asset_req_t, ASSET_REQ_MAP)

/* Transaction request */
typedef struct {
    int64_t asset_id;
    int64_t linked_asset_id;
    int64_t category_id;
    char    transaction_type[32];
    char    source_type[32];
    double  amount;
    double  price_per_unit;
    double  quantity;
    double  fee;
    char    currency[16];
    char    transaction_date[32];
    char    note[256];
} transaction_req_t;
#define TRANSACTION_REQ_MAP(_)                                                                     \
    _(transaction_req_t, asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)           \
    _(transaction_req_t, linked_asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)    \
    _(transaction_req_t, category_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)        \
    _(transaction_req_t, transaction_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)  \
    _(transaction_req_t, source_type, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)       \
    _(transaction_req_t, amount, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)             \
    _(transaction_req_t, price_per_unit, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)     \
    _(transaction_req_t, quantity, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)           \
    _(transaction_req_t, fee, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)                \
    _(transaction_req_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)          \
    _(transaction_req_t, transaction_date, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)  \
    _(transaction_req_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(transaction_req_t, TRANSACTION_REQ_MAP)

/* Daily expense request */
typedef struct {
    int64_t category_id;
    int64_t asset_id;
    char    expense_type[16];
    double  amount;
    char    expense_date[32];
    char    currency[16];
    char    note[256];
} daily_expense_req_t;
#define DAILY_EXP_REQ_MAP(_)                                                                       \
    _(daily_expense_req_t, category_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)      \
    _(daily_expense_req_t, asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)         \
    _(daily_expense_req_t, expense_type, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)    \
    _(daily_expense_req_t, amount, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)           \
    _(daily_expense_req_t, expense_date, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)    \
    _(daily_expense_req_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)        \
    _(daily_expense_req_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(daily_expense_req_t, DAILY_EXP_REQ_MAP)

/* Tag request */
typedef struct {
    char name[128];
    char color[16];
} tag_req_t;
#define TAG_REQ_MAP(_)                                                                             \
    _(tag_req_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)                     \
    _(tag_req_t, color, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(tag_req_t, TAG_REQ_MAP)

/* Transfer request */
typedef struct {
    int64_t from_asset_id;
    int64_t to_asset_id;
    double  amount;
    char    transfer_date[32];
    char    currency[16];
    char    note[256];
} transfer_req_t;
#define TRANSFER_REQ_MAP(_)                                                                        \
    _(transfer_req_t, from_asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)         \
    _(transfer_req_t, to_asset_id, CSILK_TYPE_INT64, sizeof(int64_t), 0, false, nullptr)           \
    _(transfer_req_t, amount, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)                \
    _(transfer_req_t, transfer_date, CSILK_TYPE_STRING, sizeof(char[32]), 0, true, nullptr)        \
    _(transfer_req_t, currency, CSILK_TYPE_STRING, sizeof(char[16]), 0, true, nullptr)             \
    _(transfer_req_t, note, CSILK_TYPE_STRING, sizeof(char[256]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(transfer_req_t, TRANSFER_REQ_MAP)
