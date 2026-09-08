#pragma once

/**
 * @file commands.h
 * @brief 标签用例命令对象 (Tag Application Commands)
 */

#include <stdint.h>

/** 创建标签命令 */
typedef struct {
    int64_t     user_id;
    const char* name;
    const char* color;
} create_tag_cmd_t;

/** 更新标签命令 */
typedef struct {
    int64_t     user_id;
    int64_t     tag_id;
    const char* name;
    const char* color;
} update_tag_cmd_t;

/** 删除标签命令 */
typedef struct {
    int64_t user_id;
    int64_t tag_id;
} delete_tag_cmd_t;
