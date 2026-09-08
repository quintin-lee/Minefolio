#pragma once

/**
 * @file usecases.h
 * @brief 标签用例接口声明 (Tag Application Use Cases)
 */

#include "csilk/csilk.h"
#include "application/tag/commands.h"
#include "application/tag/dtos.h"

/**
 * @brief 查询用户所有标签列表
 */
int tag_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list);

/**
 * @brief 按前缀搜索标签自动补全建议
 */
int
tag_usecase_suggestions(void* pool, int64_t user_id, const char* prefix, csilk_json_t** out_list);

/**
 * @brief 创建新标签
 */
int tag_usecase_create(void*                   pool,
                       const create_tag_cmd_t* cmd,
                       int64_t*                out_id,
                       tag_usecase_result_t*   out_res);

/**
 * @brief 更新标签
 */
int tag_usecase_update(void* pool, const update_tag_cmd_t* cmd, tag_usecase_result_t* out_res);

/**
 * @brief 删除标签
 */
int tag_usecase_delete(void* pool, const delete_tag_cmd_t* cmd, tag_usecase_result_t* out_res);
