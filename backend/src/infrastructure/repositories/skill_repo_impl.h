#pragma once
#include "csilk/csilk.h"
#include "domain/skill/entity.h"

#ifdef __cplusplus
extern "C" {
#endif

csilk_json_t*
skill_repo_list(csilk_db_pool_t* pool, int64_t user_id, const char* category, int enabled_only);
csilk_json_t* skill_repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
csilk_json_t* skill_repo_get_by_name(csilk_db_pool_t* pool, int64_t user_id, const char* name);
int64_t       skill_repo_insert(csilk_db_pool_t* pool, int64_t user_id, const mf_skill_t* skill);
int skill_repo_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const mf_skill_t* skill);
int skill_repo_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
int skill_repo_toggle(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled);

#ifdef __cplusplus
}
#endif
