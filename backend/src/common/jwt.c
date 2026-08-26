#include "jwt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char*
jwt_secret(void)
{
    return getenv("MINEFOLIO_JWT_SECRET");
}

char*
jwt_generate_token(csilk_ctx_t* c, int64_t user_id, int token_version)
{
    csilk_json_t* payload = csilk_json_object();
    int64_t       now = (int64_t)time(NULL);
    csilk_json_add_int(payload, "sub", user_id);
    csilk_json_add_int(payload, "iat", now);
    csilk_json_add_int(payload, "exp", now + 604800); /* 7 days */
    csilk_json_add_int(payload, "v", token_version);  /* token version for revocation */

    const char* secret = jwt_secret();
    if (!secret) {
        fprintf(stderr, "FATAL: JWT secret not set\n");
        csilk_json_free(payload);
        return NULL;
    }
    char* token = csilk_jwt_generate(c, payload, secret);
    csilk_json_free(payload);
    return token;
}

int64_t
jwt_get_user_id(csilk_ctx_t* c)
{
    /* Read the JWT payload WITHOUT consuming it — csilk_ctx_get_jwt_payload_json()
     * serializes, frees, and nulls the stored pointer, so calling it here would
     * make subsequent reads (e.g. jwt_validate_token_version) return nullptr. */
    csilk_json_t* payload = (csilk_json_t*)csilk_get(c, "jwt_payload");
    if (!payload) {
        return -1;
    }

    int64_t uid = (int64_t)csilk_json_get_number(payload, "sub");
    return uid;
}
