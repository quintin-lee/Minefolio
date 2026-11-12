#pragma once
#include <stdint.h>
#include "domain/skill/entity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mf_skill_repo_s {
    void* impl;
} mf_skill_repo_t;

#ifdef __cplusplus
}
#endif
