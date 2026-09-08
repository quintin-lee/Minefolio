/**
 * @file commands.h
 * @brief 账本用例命令对象 (Application Ledger Commands)
 */

#pragma once

#include <stdint.h>

/**
 * @brief 创建账本命令
 */
typedef struct {
    int64_t     user_id;
    const char* name;
    const char* description;
    const char* currency;
    const char* icon;
    const char* color;
} create_ledger_cmd_t;

/**
 * @brief 更新账本命令
 */
typedef struct {
    int64_t     ledger_id;
    int64_t     user_id;
    const char* name;
    const char* description;
    const char* currency;
    const char* icon;
    const char* color;
} update_ledger_cmd_t;

/**
 * @brief 添加成员命令
 */
typedef struct {
    int64_t     ledger_id;
    int64_t     user_id;
    const char* username;
    const char* role;
} add_member_cmd_t;

/**
 * @brief 更新成员角色命令
 */
typedef struct {
    int64_t     ledger_id;
    int64_t     user_id;
    int64_t     target_user_id;
    const char* new_role;
} update_member_cmd_t;

/**
 * @brief 移除成员命令
 */
typedef struct {
    int64_t ledger_id;
    int64_t target_user_id;
    int64_t caller_user_id;
} remove_member_cmd_t;

/**
 * @brief 加入账本命令
 */
typedef struct {
    int64_t     user_id;
    const char* invite_code;
} join_ledger_cmd_t;
