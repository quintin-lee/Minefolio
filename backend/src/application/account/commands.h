#pragma once

#include <stdint.h>

/** 创建账户命令 */
typedef struct {
    int64_t     user_id;
    int64_t     ledger_id;
    const char* name;
    const char* institution;
    const char* account_no;
    const char* currency;
    const char* icon;
    const char* note;
    int64_t     sort_order;
} create_account_cmd_t;

/** 更新账户命令 */
typedef struct {
    int64_t     user_id;
    int64_t     id;
    const char* name;
    const char* institution;
    const char* account_no;
    const char* currency;
    const char* icon;
    const char* note;
    int64_t     sort_order;
    int         has_sort_order;
    int         has_archived;
    int         archived;
} update_account_cmd_t;

/** 删除账户命令 */
typedef struct {
    int64_t user_id;
    int64_t id;
} delete_account_cmd_t;
