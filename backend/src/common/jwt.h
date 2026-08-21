#pragma once
#include "csilk/csilk.h"

/** @brief Generate a JWT token for the given user_id and token_version.
 *  The token_version is embedded as the "v" claim and validated on every request.
 *  Returns heap-allocated string (free with free()). */
char* jwt_generate_token(csilk_ctx_t* c, int64_t user_id, int token_version);

/** @brief Get user_id from JWT payload stored in context (set by jwt_middleware). */
int64_t jwt_get_user_id(csilk_ctx_t* c);
