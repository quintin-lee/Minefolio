#pragma once
#include "csilk/csilk.h"
#include "common/jwt.h"
#include "common/response.h"
#include <stdint.h>
#include <stdio.h>

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
