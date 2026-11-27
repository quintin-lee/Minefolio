#pragma once

/**
 * @file import_rule_usecases.h
 * @brief 导入分类规则应用层用例接口
 */

#include "csilk/csilk.h"
#include <stdint.h>

csilk_json_t* import_rule_usecase_list(void* pool, int64_t user_id);

csilk_json_t* import_rule_usecase_list_paged(
    void* pool, int64_t user_id, int64_t page, int64_t page_size, int64_t* out_total);

csilk_json_t* import_rule_usecase_get(void* pool, int64_t user_id, int64_t id);

int64_t import_rule_usecase_create(void*       pool,
                                   int64_t     user_id,
                                   const char* keyword,
                                   const char* match_field,
                                   const char* match_type,
                                   int64_t     category_id,
                                   const char* target_type,
                                   int         priority,
                                   int         is_active);

int import_rule_usecase_update(void*       pool,
                               int64_t     user_id,
                               int64_t     id,
                               const char* keyword,
                               const char* match_field,
                               const char* match_type,
                               int64_t     category_id,
                               const char* target_type,
                               int         priority,
                               int         is_active);

int import_rule_usecase_delete(void* pool, int64_t user_id, int64_t id);

csilk_json_t* import_rule_usecase_reset_defaults(void* pool, int64_t user_id);
