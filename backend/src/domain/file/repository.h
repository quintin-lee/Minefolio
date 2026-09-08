/**
 * @file repository.h
 * @brief 文件上传与导入领域仓储契约接口 (Domain File Repository Contract)
 */

#pragma once

#include "domain/file/entity.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 解析文件内容
 */
int mf_file_repo_parse(void*                   pool,
                       const char*             data,
                       size_t                  data_len,
                       const char*             filename,
                       mf_file_parse_result_t* out);

/**
 * @brief 获取用户的导入规则
 */
csilk_json_t* mf_file_repo_get_import_rules(void* pool, int64_t user_id);

/**
 * @brief 根据名称查找资产 ID
 * @return 资产 ID，未找到返回 0
 */
int64_t mf_file_repo_find_asset_by_name(void* pool, int64_t user_id, const char* name);

/**
 * @brief 根据名称查找分类 ID
 * @return 分类 ID，未找到返回 0
 */
int64_t mf_file_repo_find_category_by_name(void* pool, int64_t user_id, const char* name);
