#include "middlewares/cors_middleware.h"
#include <string.h>
#include <stdlib.h>

void cors_middleware_wrapper(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    /* MINEFOLIO_CORS_ORIGIN env: comma-separated origins or "*" for dev.
     * Empty / missing → defaults to "*" (same as before). */
    const char* origin = getenv("MINEFOLIO_CORS_ORIGIN");
    cors.allow_origin = origin && origin[0] ? origin : "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}

// Explicit OPTIONS handler for CORS preflight requests.
void cors_preflight_handler(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    const char* origin = getenv("MINEFOLIO_CORS_ORIGIN");
    cors.allow_origin = origin && origin[0] ? origin : "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}
