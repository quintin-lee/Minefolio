#pragma once
#include "csilk/csilk.h"

/** @brief Generate a JWT token for the given user_id. Returns heap-allocated string (free with free()). */
char* jwt_generate_token(csilk_ctx_t* c, int64_t user_id);

/** @brief Get user_id from JWT payload stored in context (set by jwt_middleware). */
int64_t jwt_get_user_id(csilk_ctx_t* c);
