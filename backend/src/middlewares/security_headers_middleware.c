#include "middlewares/security_headers_middleware.h"
#include "csilk/core/response.h"

/**
 * @brief Sets HTTP security headers on every response.
 *
 * Applied globally via csilk_app_use() so all responses carry:
 *   X-Frame-Options: DENY          — prevent clickjacking
 *   X-Content-Type-Options: nosniff — prevent MIME sniffing
 *   Referrer-Policy: strict-origin-when-cross-origin
 *
 * Note: Strict-Transport-Security (HSTS) is omitted because this server
 * may run over plain HTTP in development; HSTS should be set by the
 * reverse proxy (nginx) in production.
 */
void security_headers_middleware(csilk_ctx_t* c) {
    csilk_set_header(c, "X-Frame-Options", "DENY");
    csilk_set_header(c, "X-Content-Type-Options", "nosniff");
    csilk_set_header(c, "Referrer-Policy", "strict-origin-when-cross-origin");
    csilk_next(c);
}
