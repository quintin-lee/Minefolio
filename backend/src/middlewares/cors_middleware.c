#include "middlewares/cors_middleware.h"
#include <string.h>
#include <stdlib.h>

void
cors_middleware_wrapper(csilk_ctx_t* c)
{
    csilk_cors_config_t cors = {0};
    const char*         origin = getenv("MINEFOLIO_CORS_ORIGIN");
    if (!origin || !origin[0]) {
        const char* req_origin = csilk_get_header(c, "Origin");
        cors.allow_origin = req_origin && req_origin[0] ? req_origin : "*";
    } else {
        cors.allow_origin = origin;
    }
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}

// Explicit OPTIONS handler for CORS preflight requests.
void
cors_preflight_handler(csilk_ctx_t* c)
{
    csilk_cors_config_t cors = {0};
    const char*         origin = getenv("MINEFOLIO_CORS_ORIGIN");
    if (!origin || !origin[0]) {
        const char* req_origin = csilk_get_header(c, "Origin");
        cors.allow_origin = req_origin && req_origin[0] ? req_origin : "*";
    } else {
        cors.allow_origin = origin;
    }
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}
