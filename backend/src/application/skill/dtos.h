#pragma once
#include <stdint.h>
#include "domain/skill/entity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char                name[MF_SKILL_NAME_MAX];
    char                description[MF_SKILL_DESC_MAX];
    char                license[MF_SKILL_LICENSE_MAX];
    char                compatibility[MF_SKILL_COMPAT_MAX];
    char                category[MF_SKILL_CATEGORY_MAX];
    char                tags[MF_SKILL_TAGS_MAX];
    int                 enabled;
    const char*         instructions_l2;
    mf_skill_backend_t  backend;
    mf_skill_exec_cfg_t exec_cfg;
    int                 is_builtin;
} mf_skill_create_dto_t;

typedef struct {
    char                description[MF_SKILL_DESC_MAX];
    char                license[MF_SKILL_LICENSE_MAX];
    char                compatibility[MF_SKILL_COMPAT_MAX];
    char                category[MF_SKILL_CATEGORY_MAX];
    char                tags[MF_SKILL_TAGS_MAX];
    int                 has_enabled;
    int                 enabled;
    const char*         instructions_l2;
    mf_skill_backend_t  backend;
    mf_skill_exec_cfg_t exec_cfg;
} mf_skill_update_dto_t;

#ifdef __cplusplus
}
#endif
