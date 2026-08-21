#include "middlewares/jwt_middleware.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/core/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief Validate that the JWT's embedded token_version matches the user's
 *        current token_version in the database. Returns 0 on success, -1 on missmatch.
 *
 * Called after csilk_jwt_middleware_options() has already verified the signature
 * and expiration. The payload (with "sub" and "v" claims) is extracted from the
 * context by the middleware.
 */
static int jwt_validate_token_version(csilk_ctx_t* c, int64_t user_id) {
    /* Retrieve the "v" claim from the stored JWT payload. */
    char* json_str = csilk_ctx_get_jwt_payload_json(c);
    if (!json_str) return -1;
    csilk_json_t* root = csilk_json_parse(json_str);
    free(json_str);
    if (!root) return -1;
    int jwt_version = (int)csilk_json_get_number(root, "v");
    csilk_json_free(root);

    /* Query the user's current token_version from the database. */
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    csilk_db_pool_t* pool = db_get_pool();
    const char* params[] = { uid_str, NULL };
    csilk_json_t* row = csilk_db_query_param_json(pool,
        "SELECT token_version FROM users WHERE id=?", params);
    if (!row || csilk_json_array_size(row) == 0) {
        if (row) csilk_json_free(row);
        return -1;
    }
    int db_version = (int)db_get_int(csilk_json_array_get(row, 0), "token_version");
    csilk_json_free(row);

    return (jwt_version == db_version) ? 0 : -1;
}

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

    /* Post-JWT: validate token_version against the database. */
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid or expired token");
        csilk_abort(c);
        return;
    }
    if (jwt_validate_token_version(c, user_id) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED,
                         "Token invalidated — please log in again");
        csilk_abort(c);
        return;
    }
}
