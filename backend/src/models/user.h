#pragma once
#include <stdint.h>

typedef struct {
    int64_t id;
    char    username[128];
    char    password[256];
    char    created_at[64];
} user_t;
