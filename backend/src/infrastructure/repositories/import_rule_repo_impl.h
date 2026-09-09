#pragma once

/**
 * @file import_rule_repo_impl.h
 * @brief 基础设施层导入规则仓储实现头文件
 *
 * 提供与 legacy import_rule_repo.h 相同的函数签名，便于渐进式迁移。
 */

#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

/**
 * @brief 查询指定用户的所有账单导入匹配规则列表
 */
csilk_json_t* mf_import_rule_list(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据规则 ID 获取单条导入匹配规则详情
 */
csilk_json_t* mf_import_rule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 插入一条新的导入分类规则
 */
int64_t mf_import_rule_insert(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              const char*      keyword,
                              const char*      match_field,
                              const char*      match_type,
                              int64_t          category_id,
                              const char*      target_type,
                              int              priority,
                              int              is_active);

/**
 * @brief 更新指定的导入匹配规则
 */
int mf_import_rule_update(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          id,
                          const char*      keyword,
                          const char*      match_field,
                          const char*      match_type,
                          int64_t          category_id,
                          const char*      target_type,
                          int              priority,
                          int              is_active);

/**
 * @brief 删除指定的导入规则
 */
int mf_import_rule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 为新注册用户批量预置常用默认导入规则
 */
void mf_import_rule_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
