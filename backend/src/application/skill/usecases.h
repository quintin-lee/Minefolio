#pragma once
#include <stdint.h>
#include "csilk/csilk.h"
#include "application/skill/dtos.h"

#ifdef __cplusplus
extern "C" {
#endif

int skill_usecase_list(
    void* pool, int64_t user_id, const char* category, int enabled_only, csilk_json_t** out_data);
int skill_usecase_get(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data);
int skill_usecase_create(void*                        pool,
                         int64_t                      user_id,
                         const mf_skill_create_dto_t* dto,
                         int64_t*                     out_id);
int skill_usecase_update(void* pool, int64_t user_id, int64_t id, const mf_skill_update_dto_t* dto);
int skill_usecase_delete(void* pool, int64_t user_id, int64_t id);
int skill_usecase_toggle(void* pool, int64_t user_id, int64_t id, int enabled);
int skill_usecase_test_execute(
    void* pool, int64_t user_id, int64_t id, const char* args, csilk_json_t** out_res);
int skill_usecase_seed_defaults(void* pool, int64_t user_id);
int skill_usecase_reset_defaults(void* pool, int64_t user_id);
int
skill_usecase_import_markdown(void* pool, int64_t user_id, const char* md_content, int64_t* out_id);
int skill_usecase_export_markdown(void* pool, int64_t user_id, int64_t id, char** out_md);

#ifdef __cplusplus
}
#endif
