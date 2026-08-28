#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t id;
    int64_t owner_id;
    char name[128];
    char description[256];
    char currency[16];
    char icon[64];
    char color[32];
    bool is_default;
    char invite_code[32];
    char invite_expires_at[32];
    char created_at[32];
    char updated_at[32];
} ledger_t;

typedef struct {
    int64_t id;
    int64_t ledger_id;
    int64_t user_id;
    char username[128];
    char role[32]; /* 'owner', 'editor', 'viewer' */
    char joined_at[32];
} ledger_member_t;
