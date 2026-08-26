#pragma once
#include <stdint.h>

typedef struct {
    int64_t id;
    int64_t user_id;
    char    name[128];
    char    color[16];
    char    created_at[64];
} tag_t;
