#include "middlewares/cors_middleware.h"

void cors_middleware_wrapper(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    cors.allow_origin = "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}

// Explicit OPTIONS handler for CORS preflight requests.
void cors_preflight_handler(csilk_ctx_t* c) {
    csilk_cors_config_t cors = {0};
    cors.allow_origin = "*";
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}
