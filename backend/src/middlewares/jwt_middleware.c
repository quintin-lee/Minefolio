#include "middlewares/jwt_middleware.h"
#include "csilk/core/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void jwt_middleware_wrapper(csilk_ctx_t* c) {
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0 ||
                 strcmp(path, "/api/auth/public-key") == 0)) {
        return;
    }
    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret || secret[0] == '\0') {
        csilk_json_error(c, CSILK_STATUS_INTERNAL_SERVER_ERROR,
                         "MINEFOLIO_JWT_SECRET environment variable is required");
        csilk_abort(c);
        return;
    }
    csilk_jwt_options_t opts = {
        .algorithm = CSILK_JWT_HS256,
        .flags = CSILK_JWT_REQUIRE_EXP,
        .leeway_sec = 0,
    };
    csilk_jwt_middleware_options(c, secret, strlen(secret), &opts);
}
