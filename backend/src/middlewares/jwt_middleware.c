#include "middlewares/jwt_middleware.h"
#include "common/db.h"
#include "common/jwt.h"
#include "csilk/core/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief Validate that the JWT's embedded token_version matches the user's
 *        current token_version in the database. Returns 0 on success, -1 on mismatch.
 *
 * This function queries the database to compare the token version claim in the
 * JWT payload against the current token_version stored for the user.
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

/**
 * @brief JWT authentication middleware wrapper with custom token_version validation.
 *
 * This wrapper implements the correct middleware pattern:
 * 1. Check exempt paths first, call csilk_next(c) and return
 * 2. Verify secret configuration
 * 3. Extract Authorization header manually
 * 4. Verify JWT signature and expiration (WITHOUT calling csilk_next)
 * 5. Store payload in context via csilk_set_ex()
 * 6. Validate token_version against database
 * 7. Call csilk_next(c) ONCE after all checks pass
 *
 * Note: We use csilk_jwt_verify_options() instead of csilk_jwt_middleware_options()
 * because the latter calls csilk_next() internally, which would execute the
 * business handler BEFORE our token_version check runs.
 */
void jwt_middleware_wrapper(csilk_ctx_t* c) {
    if (!c) return;

    /* 1. Match whitelist/exempt paths */
    const char* path = csilk_get_path(c);
    if (path && (strcmp(path, "/api/auth/login") == 0 ||
                 strcmp(path, "/api/auth/register") == 0 ||
                 strcmp(path, "/api/system/status") == 0 ||
                 strcmp(path, "/api/system/setup") == 0 ||
                 strcmp(path, "/api/auth/public-key") == 0)) {
        csilk_next(c);
        return;
    }

    /* 2. Check secret configuration */
    const char* secret = getenv("MINEFOLIO_JWT_SECRET");
    if (!secret || secret[0] == '\0') {
        csilk_json_error(c, CSILK_STATUS_INTERNAL_SERVER_ERROR,
                         "MINEFOLIO_JWT_SECRET environment variable is required");
        csilk_abort(c);
        return;
    }

    /* 3. Extract Authorization header */
    const char* auth_header = csilk_get_header(c, "Authorization");
    if (!auth_header || strncmp(auth_header, "Bearer ", 7) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Bearer token required");
        csilk_abort(c);
        return;
    }
    const char* token = auth_header + 7;

    /* 4. Verify JWT signature and expiration (does NOT call csilk_next) */
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

    /* Store payload in context (RAII auto-free) */
    csilk_set_ex(c, "jwt_payload", payload, (void (*)(void*))csilk_json_free);

    /* 5. Validate database token_version (BEFORE executing business handler) */
    int64_t user_id = jwt_get_user_id(c);
    if (user_id < 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "Invalid user in token claims");
        csilk_abort(c);
        return;
    }
    if (jwt_validate_token_version(c, user_id) != 0) {
        csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED,
                         "Token invalidated — please log in again");
        csilk_abort(c);
        return;
    }

    /* 6. All checks passed, pass to next middleware/handler */
    csilk_next(c);
}
