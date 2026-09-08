#pragma once

/**
 * @file repository.h
 * @brief 分类仓储抽象契约接口 (Domain Category Repository Contract)
 *
 * 纯 C 契约，入参出参仅允许领域实体与标量数值。
 */

#include <stdint.h>
#include <stddef.h>
#include "domain/category/entity.h"

/**
 * @brief 查询用户分类列表（可选按 type 过滤）
 * @return 0: 成功, -1: 数据库错误
 */
int mf_category_repo_list(
    void* db_pool, int64_t user_id, const char* type, mf_category_t** out_list, size_t* out_count);

/**
 * @brief 查询分类的直接子分类
 * @return 0: 成功, -1: 数据库错误
 */
int mf_category_repo_children(
    void* db_pool, int64_t user_id, int64_t parent_id, mf_category_t** out_list, size_t* out_count);

/**
 * @brief 统计指定分类下的直接子分类数量
 * @return 0: 成功, -1: 数据库错误
 */
int mf_category_repo_count_children(void*    db_pool,
                                    int64_t  user_id,
                                    int64_t  parent_id,
                                    int64_t* out_count);

/**
 * @brief 持久化保存新分类
 * @return 0: 成功, -1: 失败
 */
int
mf_category_repo_create(void* db_pool, int64_t user_id, const mf_category_t* cat, int64_t* out_id);

/**
 * @brief 更新分类属性
 * @return 0: 成功, 1: 不存在, -1: 数据库错误
 */
int mf_category_repo_update(void* db_pool, int64_t user_id, int64_t id, const mf_category_t* cat);

/**
 * @brief 删除指定分类
 * @return 0: 成功, 1: 不存在, -1: 数据库错误
 */
int mf_category_repo_delete(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 查找同名同层级分类，若不存在则创建（find-or-create）
 * @return 分类 ID (>0), 失败返回 0
 */
int64_t mf_category_repo_find_or_create(void*       db_pool,
                                        int64_t     user_id,
                                        const char* name,
                                        int64_t     parent_id,
                                        const char* type,
                                        const char* asset_type,
                                        const char* icon,
                                        int         sort_order);

/**
 * @brief 检查分类是否存在且属于该用户
 * @return 1: 存在, 0: 不存在
 */
int mf_category_repo_exists(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 检查用户是否已完成默认分类播种
 * @return 1: 已播种, 0: 未播种
 */
int mf_category_repo_is_seeded(void* db_pool, int64_t user_id);

/**
 * @brief 标记用户已完成默认分类播种
 */
void mf_category_repo_mark_seeded(void* db_pool, int64_t user_id);

/**
 * @brief 释放由仓储分配的实体数组内存
 */
void mf_category_repo_free_list(mf_category_t* list, size_t count);
