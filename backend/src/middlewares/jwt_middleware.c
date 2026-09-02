#include "middlewares/jwt_middleware.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/core/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int
jwt_validate_token_version(csilk_ctx_t* c, int64_t user_id)
{
    csilk_json_t* root = (csilk_json_t*)csilk_get(c, "jwt_payload");
    if (!root) {
        return -1;
    }
    int jwt_version = (int)csilk_json_get_number(root, "v");

    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_db_pool_t* pool = db_get_pool();
    const char*      params[] = {uid_str, NULL};
    csilk_json_t*    row =
        csilk_db_query_param_json(pool, "SELECT token_version FROM users WHERE id=?", params);
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) {
            csilk_json_free(row);
        }
        return -1;
    }
    int db_version = (int)db_get_int(csilk_json_array_get(row, 0), "token_version");
    csilk_json_free(row);

    return (jwt_version == db_version) ? 0 : -1;
}

void
jwt_middleware_wrapper(csilk_ctx_t* c)
{
    if (!c) {
        return;
    }

    const char* path = csilk_get_path(c);
    if (path &&
        (strcmp(path, "/api/auth/login") == 0 || strcmp(path, "/api/auth/register") == 0 ||
         strcmp(path, "/api/system/status") == 0 || strcmp(path, "/api/system/setup") == 0 ||
         strcmp(path, "/api/auth/public-key") == 0 || strcmp(path, "/api/ai/workflows") == 0 ||
         strcmp(path, "/api/auth/2fa/verify-login") == 0 ||
         strcmp(path, "/api/auth/oauth/providers") == 0 ||
         strcmp(path, "/api/auth/oauth/callback") == 0)) {
        csilk_next(c);
        return;
    }

    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret || secret[0] == '\0') {
        csilk_json_error(c,
                         CSILK_STATUS_INTERNAL_SERVER_ERROR,
                         "MINEFOLIO_JWT_SECRET environment variable is required");
        csilk_abort(c);
        return;
    }

    const char* auth_header = csilk_get_header(c, "Authorization");
    if (!auth_header || strncmp(auth_header, "Bearer ", 7) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Bearer token required");
        csilk_abort(c);
        return;
    }
    const char* token = auth_header + 7;

    csilk_jwt_options_t opts = {
        .algorithm = CSILK_JWT_HS256,
        .flags = CSILK_JWT_REQUIRE_EXP,
        .leeway_sec = 0,
    };
    csilk_json_t* payload = csilk_jwt_verify_options(c, token, secret, strlen(secret), &opts);
    if (!payload) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid or expired token");
        csilk_abort(c);
        return;
    }

    csilk_set_ex(c, "jwt_payload", payload, (void (*)(void*))csilk_json_free);

    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid user in token claims");
        csilk_abort(c);
        return;
    }
    if (jwt_validate_token_version(c, user_id) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Token invalidated — please log in again");
        csilk_abort(c);
        return;
    }

    csilk_next(c);
}
