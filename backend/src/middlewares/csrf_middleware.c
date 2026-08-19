#include "middlewares/csrf_middleware.h"
#include <string.h>

void csrf_middleware_wrapper(csilk_ctx_t* c) {
    const char* method = csilk_get_method(c);
    if (!method) { csilk_next(c); return; }

    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
        csilk_next(c);
        return;
    }

    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
        strcmp(method, "OPTIONS") == 0) {
        if (!csilk_get_cookie(c, "csrf_token")) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
            }
        }
        csilk_next(c);
        return;
    }

    const char* token = csilk_get_header(c, "X-CSRF-Token");
    const char* cookie = csilk_get_cookie(c, "csrf_token");
    if (!token || !cookie || strcmp(token, cookie) != 0) {
        csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: Invalid CSRF token");
        csilk_abort(c);
        return;
    }
    csilk_next(c);
}
