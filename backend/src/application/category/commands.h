#pragma once

/**
 * @file commands.h
 * @brief 分类用例命令对象 (Category Application Commands)
 */

#include <stdint.h>

/** 创建分类命令 */
typedef struct {
    int64_t     user_id;
    const char* name;
    int64_t     parent_id;
    const char* type;
    const char* asset_type;
    const char* currency;
    const char* icon;
    int         sort_order;
} create_category_cmd_t;

/** 更新分类命令 */
typedef struct {
    int64_t     user_id;
    int64_t     category_id;
    const char* name;
    const char* type;
    const char* asset_type;
    const char* currency;
    const char* icon;
    int         sort_order;
} update_category_cmd_t;

/** 删除分类命令 */
typedef struct {
    int64_t user_id;
    int64_t category_id;
} delete_category_cmd_t;
