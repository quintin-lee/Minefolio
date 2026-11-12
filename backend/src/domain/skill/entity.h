#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MF_SKILL_NAME_MAX 64
#define MF_SKILL_DESC_MAX 1024
#define MF_SKILL_LICENSE_MAX 64
#define MF_SKILL_COMPAT_MAX 500
#define MF_SKILL_CATEGORY_MAX 64
#define MF_SKILL_TAGS_MAX 256
#define MF_SKILL_PATH_MAX 256
#define MF_SKILL_ENV_MAX 512
#define MF_SKILL_ARGS_MAX 512

typedef enum {
    MF_SKILL_BACKEND_LOCAL = 0,
    MF_SKILL_BACKEND_REMOTE = 1,
} mf_skill_backend_t;

typedef struct {
    char root[MF_SKILL_PATH_MAX];
    char path[MF_SKILL_PATH_MAX];
    char args[MF_SKILL_ARGS_MAX];
    char env[MF_SKILL_ENV_MAX];
    int timeout_ms;
    int max_output_bytes;
} mf_skill_exec_cfg_t;

typedef struct {
    int64_t id;
    int64_t user_id;
    char name[MF_SKILL_NAME_MAX];
    char description[MF_SKILL_DESC_MAX];
    char license[MF_SKILL_LICENSE_MAX];
    char compatibility[MF_SKILL_COMPAT_MAX];
    char category[MF_SKILL_CATEGORY_MAX];
    char tags[MF_SKILL_TAGS_MAX];
    int enabled;
    char* instructions_l2; // Dynamically allocated or pointer to markdown text
    mf_skill_backend_t backend;
    mf_skill_exec_cfg_t exec_cfg;
    int is_builtin;
    char created_at[32];
    char updated_at[32];
} mf_skill_t;
