#include "middlewares/jwt_middleware.h"
#include <string.h>
#include <stdlib.h>

void jwt_middleware_wrapper(csilk_ctx_t* c) {
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
        return;
    }
    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret) secret = "minefolio-dev-secret-change-in-production";
    csilk_jwt_middleware(c, secret);
}
