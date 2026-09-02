/**
 * @file security_headers_middleware.c
 * @brief HTTP 基础安全响应头注入中间件实现
 *
 * 实现了在响应头中统一写入 X-Frame-Options、X-Content-Type-Options、
 * 和 Referrer-Policy 的逻辑。
 */

#include "middlewares/security_headers_middleware.h"
#include "csilk/core/response.h"

/**
 * @brief 设置全局 HTTP 基础安全响应头
 *
 * 通过 csilk_app_use() 注册为全局中间件，为每个响应添加：
 *   - X-Frame-Options: DENY          — 防范点击劫持
 *   - X-Content-Type-Options: nosniff — 防范 MIME 类型混淆嗅探
 *   - Referrer-Policy: strict-origin-when-cross-origin
 *
 * 注：Strict-Transport-Security (HSTS) 在此未配置，因为开发环境中可能以纯 HTTP 运行；
 * 生产环境应由前置反向代理（如 Nginx）统一配置 HSTS。
 *
 * @param[in,out] c HTTP 请求上下文
 */
void
security_headers_middleware(csilk_ctx_t* c)
{
    csilk_set_header(c, "X-Frame-Options", "DENY");
    csilk_set_header(c, "X-Content-Type-Options", "nosniff");
    csilk_set_header(c, "Referrer-Policy", "strict-origin-when-cross-origin");
    csilk_next(c);
}
