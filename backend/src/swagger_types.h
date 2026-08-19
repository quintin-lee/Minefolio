#pragma once
#include "csilk/reflection/reflect.h"

/* ================================================================= */
/* Request body types                                                */
/* ================================================================= */

typedef struct {
    char username[128];
    char password_enc[512];
    char db_driver[64];
    char db_dsn[512];
} minefolio_setup_req_t;
#define SETUP_REQ_MAP(_)                                                            \
    _(minefolio_setup_req_t, username,    CSILK_TYPE_STRING,  sizeof(char[128]),     0, true,  nullptr) \
    _(minefolio_setup_req_t, password_enc, CSILK_TYPE_STRING,  sizeof(char[512]),     0, true,  nullptr) \
    _(minefolio_setup_req_t, db_driver,   CSILK_TYPE_STRING,  sizeof(char[64]),      0, true,  nullptr) \
    _(minefolio_setup_req_t, db_dsn,      CSILK_TYPE_STRING,  sizeof(char[512]),     0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_setup_req_t, SETUP_REQ_MAP)

typedef struct {
    char username[128];
    char password[128];
} minefolio_register_req_t;
#define REGISTER_REQ_MAP(_)                                                        \
    _(minefolio_register_req_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr) \
    _(minefolio_register_req_t, password, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(minefolio_register_req_t, REGISTER_REQ_MAP)

typedef struct {
    char username[128];
    char password_enc[512];
} minefolio_login_req_t;
#define LOGIN_REQ_MAP(_)                                                            \
    _(minefolio_login_req_t, username,    CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_login_req_t, password_enc, CSILK_TYPE_STRING, sizeof(char[512]), 0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_login_req_t, LOGIN_REQ_MAP)

typedef struct {
    char old_password_enc[512];
    char new_password_enc[512];
} minefolio_change_pwd_req_t;
#define CHG_PWD_REQ_MAP(_)                                                          \
    _(minefolio_change_pwd_req_t, old_password_enc,  CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr) \
    _(minefolio_change_pwd_req_t, new_password_enc,  CSILK_TYPE_STRING, sizeof(char[512]), 0, true, nullptr)
CSILK_REGISTER_REFLECT(minefolio_change_pwd_req_t, CHG_PWD_REQ_MAP)

typedef struct {
    char name[128];
    char type[32];
    char asset_type[32];
    char currency[16];
    char icon[64];
    int64_t parent_id;
    int sort_order;
} minefolio_category_req_t;
#define CATEGORY_REQ_MAP(_)                                                          \
    _(minefolio_category_req_t, name,      CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_category_req_t, type,      CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_category_req_t, asset_type,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_category_req_t, currency,  CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_category_req_t, icon,      CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr) \
    _(minefolio_category_req_t, parent_id, CSILK_TYPE_INT64,  sizeof(int64_t),   0, false, nullptr) \
    _(minefolio_category_req_t, sort_order,CSILK_TYPE_INT32,  sizeof(int32_t),   0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_category_req_t, CATEGORY_REQ_MAP)

typedef struct {
    char name[128];
    char account_no[64];
    char currency[16];
    char note[256];
    int64_t category_id;
    double current_value;
    double quantity;
    double cost_basis;
    double net_value;
} minefolio_asset_req_t;
#define ASSET_REQ_MAP(_)                                                              \
    _(minefolio_asset_req_t, name,       CSILK_TYPE_STRING, sizeof(char[128]),  0, true,  nullptr) \
    _(minefolio_asset_req_t, account_no, CSILK_TYPE_STRING, sizeof(char[64]),   0, true,  nullptr) \
    _(minefolio_asset_req_t, currency,   CSILK_TYPE_STRING, sizeof(char[16]),   0, true,  nullptr) \
    _(minefolio_asset_req_t, note,       CSILK_TYPE_STRING, sizeof(char[256]),  0, true,  nullptr) \
    _(minefolio_asset_req_t, category_id,CSILK_TYPE_INT64,  sizeof(int64_t),   0, false, nullptr) \
    _(minefolio_asset_req_t, current_value,CSILK_TYPE_DOUBLE,sizeof(double),   0, false, nullptr) \
    _(minefolio_asset_req_t, quantity,   CSILK_TYPE_DOUBLE, sizeof(double),    0, false, nullptr) \
    _(minefolio_asset_req_t, cost_basis, CSILK_TYPE_DOUBLE, sizeof(double),    0, false, nullptr) \
    _(minefolio_asset_req_t, net_value,  CSILK_TYPE_DOUBLE, sizeof(double),    0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_asset_req_t, ASSET_REQ_MAP)

typedef struct {
    char currency[16];
    char note[256];
    char transaction_date[32];
    int64_t asset_id;
    int64_t linked_asset_id;
    int64_t category_id;
    double amount;
    double price_per_unit;
    double quantity;
    double fee;
    char transaction_type[32];
    char source_type[32];
} minefolio_transaction_req_t;
#define TRANSACTION_REQ_MAP(_)                                                         \
    _(minefolio_transaction_req_t, asset_id,        CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_transaction_req_t, linked_asset_id, CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_transaction_req_t, category_id,     CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_transaction_req_t, transaction_type,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr)  \
    _(minefolio_transaction_req_t, source_type,     CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr)  \
    _(minefolio_transaction_req_t, amount,          CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_req_t, price_per_unit,  CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_req_t, quantity,        CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_req_t, fee,             CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_req_t, currency,        CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr)  \
    _(minefolio_transaction_req_t, transaction_date,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr)  \
    _(minefolio_transaction_req_t, note,            CSILK_TYPE_STRING, sizeof(char[256]), 0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_transaction_req_t, TRANSACTION_REQ_MAP)

typedef struct {
    char currency[16];
    char expense_date[32];
    char note[256];
    int64_t category_id;
    int64_t asset_id;
    double amount;
    char expense_type[16];
} minefolio_daily_expense_req_t;
#define DAILY_EXP_REQ_MAP(_)                                                          \
    _(minefolio_daily_expense_req_t, category_id,  CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_daily_expense_req_t, asset_id,     CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_daily_expense_req_t, expense_type, CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr)  \
    _(minefolio_daily_expense_req_t, amount,       CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_daily_expense_req_t, expense_date, CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr)  \
    _(minefolio_daily_expense_req_t, currency,     CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr)  \
    _(minefolio_daily_expense_req_t, note,         CSILK_TYPE_STRING, sizeof(char[256]), 0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_daily_expense_req_t, DAILY_EXP_REQ_MAP)

typedef struct {
    char name[128];
    char color[16];
} minefolio_tag_req_t;
#define TAG_REQ_MAP(_)                                                              \
    _(minefolio_tag_req_t, name, CSILK_TYPE_STRING, sizeof(char[128]), 0, true, nullptr) \
    _(minefolio_tag_req_t, color, CSILK_TYPE_STRING, sizeof(char[16]),  0, true, nullptr)
CSILK_REGISTER_REFLECT(minefolio_tag_req_t, TAG_REQ_MAP)

typedef struct {
    char currency[16];
    char note[256];
    char transfer_date[32];
    int64_t from_asset_id;
    int64_t to_asset_id;
    double amount;
} minefolio_transfer_req_t;
#define TRANSFER_REQ_MAP(_)                                                          \
    _(minefolio_transfer_req_t, from_asset_id, CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr) \
    _(minefolio_transfer_req_t, to_asset_id,   CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr) \
    _(minefolio_transfer_req_t, amount,        CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_transfer_req_t, transfer_date, CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_transfer_req_t, currency,      CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_transfer_req_t, note,          CSILK_TYPE_STRING, sizeof(char[256]), 0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_transfer_req_t, TRANSFER_REQ_MAP)

/* ================================================================= */
/* Response body types                                               */
/* ================================================================= */

typedef struct {
    char token[512];
    double expires_in;
} minefolio_token_resp_t;
#define TOKEN_RESP_MAP(_)                                                             \
    _(minefolio_token_resp_t, token,      CSILK_TYPE_STRING, sizeof(char[512]), 0, true,  nullptr) \
    _(minefolio_token_resp_t, expires_in, CSILK_TYPE_DOUBLE, sizeof(double),   0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_token_resp_t, TOKEN_RESP_MAP)

typedef struct {
    int64_t id;
    char username[128];
    char created_at[64];
} minefolio_user_resp_t;
#define USER_RESP_MAP(_)                                                              \
    _(minefolio_user_resp_t, id,       CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr) \
    _(minefolio_user_resp_t, username, CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_user_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]), 0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_user_resp_t, USER_RESP_MAP)

typedef struct {
    int64_t id;
    char name[128];
    char parent_name[128];
    char type[32];
    char asset_type[32];
    char currency[16];
    char icon[64];
    int64_t parent_id;
    int sort_order;
} minefolio_category_resp_t;
#define CATEGORY_RESP_MAP(_)                                                             \
    _(minefolio_category_resp_t, id,        CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_category_resp_t, name,      CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_category_resp_t, parent_name,CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_category_resp_t, type,      CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_category_resp_t, asset_type,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_category_resp_t, currency,  CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_category_resp_t, icon,      CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr) \
    _(minefolio_category_resp_t, parent_id, CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_category_resp_t, sort_order,CSILK_TYPE_INT32,  sizeof(int32_t), 0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_category_resp_t, CATEGORY_RESP_MAP)

typedef struct {
    int64_t id;
    char name[128];
    char account_no[64];
    char currency[16];
    char note[256];
    char category_name[128];
    char asset_type[32];
    int64_t category_id;
    double current_value;
    double quantity;
    double cost_basis;
    double net_value;
    char created_at[64];
    char updated_at[64];
} minefolio_asset_resp_t;
#define ASSET_RESP_MAP(_)                                                               \
    _(minefolio_asset_resp_t, id,         CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_asset_resp_t, name,       CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_asset_resp_t, account_no, CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr) \
    _(minefolio_asset_resp_t, currency,   CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_asset_resp_t, note,       CSILK_TYPE_STRING, sizeof(char[256]), 0, true,  nullptr) \
    _(minefolio_asset_resp_t, category_name,CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_asset_resp_t, asset_type, CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_asset_resp_t, category_id,CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_asset_resp_t, current_value,CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_asset_resp_t, quantity,   CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_asset_resp_t, cost_basis, CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_asset_resp_t, net_value,  CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_asset_resp_t, created_at, CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr) \
    _(minefolio_asset_resp_t, updated_at, CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_asset_resp_t, ASSET_RESP_MAP)

typedef struct {
    int64_t id;
    char name[128];
    char color[16];
    char created_at[64];
} minefolio_tag_resp_t;
#define TAG_RESP_MAP(_)                                                              \
    _(minefolio_tag_resp_t, id,       CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr) \
    _(minefolio_tag_resp_t, name,     CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_tag_resp_t, color,    CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_tag_resp_t, created_at,CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_tag_resp_t, TAG_RESP_MAP)

typedef struct {
    int64_t id;
    int64_t asset_id;
    char transaction_type[32];
    double amount;
    double quantity;
    double price_per_unit;
    char currency[16];
    char transaction_date[32];
    char note[256];
    char created_at[64];
} minefolio_transaction_resp_t;
#define TX_RESP_MAP(_)                                                               \
    _(minefolio_transaction_resp_t, id,              CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_transaction_resp_t, asset_id,        CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_transaction_resp_t, transaction_type,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_transaction_resp_t, amount,          CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_resp_t, quantity,        CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_resp_t, price_per_unit,  CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)  \
    _(minefolio_transaction_resp_t, currency,        CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_transaction_resp_t, transaction_date,CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_transaction_resp_t, note,            CSILK_TYPE_STRING, sizeof(char[256]), 0, true,  nullptr)  \
    _(minefolio_transaction_resp_t, created_at,      CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr)
CSILK_REGISTER_REFLECT(minefolio_transaction_resp_t, TX_RESP_MAP)

typedef struct {
    int64_t id;
    char name[128];
    char asset_type[32];
    char currency[16];
    double quantity;
    double net_value;
    double cost_basis;
    double current_value;
    double floating_pnl;
    double floating_pct;
    double realized_pnl;
} minefolio_holdings_item_resp_t;
#define HOLDINGS_ITEM_MAP(_)                                                           \
    _(minefolio_holdings_item_resp_t, id,         CSILK_TYPE_INT64,  sizeof(int64_t), 0, false, nullptr)  \
    _(minefolio_holdings_item_resp_t, name,       CSILK_TYPE_STRING, sizeof(char[128]), 0, true,  nullptr) \
    _(minefolio_holdings_item_resp_t, asset_type, CSILK_TYPE_STRING, sizeof(char[32]),  0, true,  nullptr) \
    _(minefolio_holdings_item_resp_t, currency,   CSILK_TYPE_STRING, sizeof(char[16]),  0, true,  nullptr) \
    _(minefolio_holdings_item_resp_t, quantity,   CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, net_value,  CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, cost_basis, CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, current_value,CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, floating_pnl, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, floating_pct, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr) \
    _(minefolio_holdings_item_resp_t, realized_pnl, CSILK_TYPE_DOUBLE, sizeof(double), 0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_holdings_item_resp_t, HOLDINGS_ITEM_MAP)

typedef struct {
    char label[64];
    double net_worth;
    double assets;
    double liabilities;
} minefolio_asset_trend_point_resp_t;
#define TREND_POINT_MAP(_)                                                           \
    _(minefolio_asset_trend_point_resp_t, label,     CSILK_TYPE_STRING, sizeof(char[64]),  0, true,  nullptr) \
    _(minefolio_asset_trend_point_resp_t, net_worth, CSILK_TYPE_DOUBLE, sizeof(double),   0, false, nullptr) \
    _(minefolio_asset_trend_point_resp_t, assets,    CSILK_TYPE_DOUBLE, sizeof(double),   0, false, nullptr) \
    _(minefolio_asset_trend_point_resp_t, liabilities,CSILK_TYPE_DOUBLE, sizeof(double),  0, false, nullptr)
CSILK_REGISTER_REFLECT(minefolio_asset_trend_point_resp_t, TREND_POINT_MAP)
