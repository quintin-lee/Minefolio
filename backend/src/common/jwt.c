#include "jwt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char* jwt_secret(void) {
    const char* s = getenv("MINEFOLIO_JWT_SECRET");
    return s ? s : "minefolio-dev-secret-change-in-production";
}

char* jwt_generate_token(csilk_ctx_t* c, int64_t user_id, int token_version) {
    csilk_json_t* payload = csilk_json_object();
    int64_t now = (int64_t)time(NULL);
    csilk_json_add_int(payload, "sub", user_id);
    csilk_json_add_int(payload, "iat", now);
    csilk_json_add_int(payload, "exp", now + 604800); /* 7 days */
    csilk_json_add_int(payload, "v", token_version);  /* token version for revocation */

    char* secret = (char*)jwt_secret();
    char* token = csilk_jwt_generate(c, payload, secret);
    csilk_json_free(payload);
    return token;
}

int64_t jwt_get_user_id(csilk_ctx_t* c) {
    char* json_str = csilk_ctx_get_jwt_payload_json(c);
    if (!json_str) return -1;

    csilk_json_t* root = csilk_json_parse(json_str);
    free(json_str);
    if (!root) return -1;

    int64_t uid = (int64_t)csilk_json_get_number(root, "sub");
    csilk_json_free(root);
    return uid;
}
