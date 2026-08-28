#pragma once
#include "csilk/csilk.h"
#include "common/jwt.h"
#include "common/response.h"
#include "repositories/ledger_repo.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Retrieve authenticated user_id; returns -1 and sends 401 on failure. */
static inline int64_t
ctx_user_id(csilk_ctx_t* c)
{
    int64_t uid = jwt_get_user_id(c);
    if (uid < 0) {
        respond_unauthorized(c);
        return -1;
    }
    return uid;
}

/** @brief Format user_id as a fixed-width string for use in SQL params. */
static inline void
ctx_uid_str(int64_t user_id, char out[static 32])
{
    snprintf(out, 32, "%lld", (long long)user_id);
}

/**
 * @brief Retrieve and validate ledger_id for the current context.
 * 
 * 1. Checks Header "X-Ledger-Id", Query "ledger_id".
 * 2. If not specified, falls back to user's default ledger.
 * 3. Verifies user is a member of the ledger and satisfies required_role ('owner', 'editor', 'viewer').
 * 4. On failure, responds with 1004 Forbidden and returns -1.
 */
static inline int64_t
ctx_ledger_id(csilk_ctx_t* c, int64_t user_id, const char* required_role)
{
    if (user_id <= 0) {
        return -1;
    }

    const char* lid_header = csilk_get_header(c, "X-Ledger-Id");
    const char* lid_query = csilk_get_query(c, "ledger_id");
    int64_t     lid = 0;
    if (lid_header && lid_header[0]) {
        lid = atoll(lid_header);
    } else if (lid_query && lid_query[0]) {
        lid = atoll(lid_query);
    }

    csilk_db_pool_t* pool = db_get_pool();
    if (lid <= 0) {
        lid = ledger_get_default(pool, user_id);
    }
    if (lid <= 0) {
        respond_error(c, 1004, "No active ledger found");
        return -1;
    }

    if (required_role && required_role[0]) {
        char role[32] = {0};
        if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
            respond_error(c, 1004, "Forbidden: you are not a member of this ledger");
            return -1;
        }

        if (strcmp(required_role, "owner") == 0) {
            if (strcmp(role, "owner") != 0) {
                respond_error(c, 1004, "Forbidden: owner permission required");
                return -1;
            }
        } else if (strcmp(required_role, "editor") == 0) {
            if (strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0) {
                respond_error(c, 1004, "Forbidden: editor permission required (read-only mode)");
                return -1;
            }
        }
    }

    return lid;
}
