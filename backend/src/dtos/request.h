#pragma once
#include <stdint.h>

/* System setup request */
typedef struct {
    char username[128];
    char password_enc[512];
    char db_driver[64];
    char db_dsn[512];
} setup_req_t;

/* Auth requests */
typedef struct {
    char username[128];
    char password[128];
} register_req_t;

typedef struct {
    char username[128];
    char password_enc[512];
} login_req_t;

typedef struct {
    char old_password_enc[512];
    char new_password_enc[512];
} change_pwd_req_t;

/* Category request */
typedef struct {
    char     name[128];
    char     type[32];
    char     asset_type[32];
    char     currency[16];
    char     icon[64];
    int64_t  parent_id;
    int      sort_order;
} category_req_t;

/* Asset request */
typedef struct {
    char     name[128];
    char     account_no[64];
    char     currency[16];
    char     note[256];
    int64_t  category_id;
    double   current_value;
    double   quantity;
    double   cost_basis;
    double   net_value;
} asset_req_t;

/* Transaction request */
typedef struct {
    int64_t  asset_id;
    int64_t  linked_asset_id;
    int64_t  category_id;
    char     transaction_type[32];
    char     source_type[32];
    double   amount;
    double   price_per_unit;
    double   quantity;
    double   fee;
    char     currency[16];
    char     transaction_date[32];
    char     note[256];
} transaction_req_t;

/* Daily expense request */
typedef struct {
    int64_t  category_id;
    int64_t  asset_id;
    char     expense_type[16];
    double   amount;
    char     expense_date[32];
    char     currency[16];
    char     note[256];
} daily_expense_req_t;

/* Tag request */
typedef struct {
    char name[128];
    char color[16];
} tag_req_t;

/* Transfer request */
typedef struct {
    int64_t  from_asset_id;
    int64_t  to_asset_id;
    double   amount;
    char     transfer_date[32];
    char     currency[16];
    char     note[256];
} transfer_req_t;
