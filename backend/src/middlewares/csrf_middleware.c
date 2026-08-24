#include "middlewares/csrf_middleware.h"
#include "csilk/core/response.h"
#include <string.h>
#include <stdio.h>

void csrf_middleware_wrapper(csilk_ctx_t* c) {
    const char* method = csilk_get_method(c);
    if (!method) { csilk_next(c); return; }

    /* On safe methods (GET/HEAD/OPTIONS), ensure client has a csrf_token cookie */
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

    /* For mutating methods (POST/PUT/DELETE): check exemptions */
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0)) {
        if (!csilk_get_cookie(c, "csrf_token")) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
            }
        }
        csilk_next(c);
        return;
    }

    /* Validate CSRF token */
    const char* token = csilk_get_header(c, "X-CSRF-Token");
    const char* cookie = csilk_get_cookie(c, "csrf_token");

    if (cookie && cookie[0]) {
        /* When cookie is present, X-CSRF-Token header MUST match the cookie value */
        if (!token || strcmp(token, cookie) != 0) {
            csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: Invalid CSRF token");
            csilk_abort(c);
            return;
        }
    } else {
        /* If no CSRF cookie was stored yet, allow only if request carries Bearer auth header */
        const char* auth_hdr = csilk_get_header(c, "Authorization");
        if (!auth_hdr || strncmp(auth_hdr, "Bearer ", 7) != 0) {
            csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: CSRF cookie missing");
            csilk_abort(c);
            return;
        }
        /* Issue CSRF cookie for subsequent requests */
        char buf[33];
        if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
            csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
        }
    }
    csilk_next(c);
}
