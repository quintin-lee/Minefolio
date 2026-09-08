#pragma once

/**
 * @file usecases.h
 * @brief 分类用例接口声明 (Category Application Use Cases)
 */

#include "csilk/csilk.h"
#include "application/category/commands.h"
#include "application/category/dtos.h"

/**
 * @brief 确保用户已播种默认分类（幂等）
 */
void category_usecase_seed_defaults(void* pool, int64_t user_id);

/**
 * @brief 查询用户分类列表（可选按 type 过滤），返回 JSON 数组
 */
int category_usecase_list(void* pool, int64_t user_id, const char* type, csilk_json_t** out_list);

/**
 * @brief 查询分类的直接子分类，返回 JSON 数组
 */
int
category_usecase_children(void* pool, int64_t user_id, int64_t parent_id, csilk_json_t** out_list);

/**
 * @brief 创建新分类
 */
int category_usecase_create(void*                        pool,
                            const create_category_cmd_t* cmd,
                            int64_t*                     out_id,
                            category_usecase_result_t*   out_res);

/**
 * @brief 更新分类
 */
int category_usecase_update(void*                        pool,
                            const update_category_cmd_t* cmd,
                            category_usecase_result_t*   out_res);

/**
 * @brief 删除分类（有子分类时拒绝）
 */
int category_usecase_delete(void*                        pool,
                            const delete_category_cmd_t* cmd,
                            category_usecase_result_t*   out_res);
