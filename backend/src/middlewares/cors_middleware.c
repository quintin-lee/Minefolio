/**
 * @file cors_middleware.c
 * @brief 跨域资源共享 (CORS) 中间件与预检请求处理实现
 *
 * 实现了跨域资源共享头部配置、请求来源（Origin）解析与 OPTIONS 预检请求响应逻辑。
 */

#include "middlewares/cors_middleware.h"
#include "config/secret.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief 全局 CORS 中间件包装函数
 *
 * 检查环境配置与请求头以动态构建 csilk_cors_config_t 配置并执行 CORS 处理流程。
 *
 * @param[in,out] c HTTP 请求上下文
 */
void
cors_middleware_wrapper(csilk_ctx_t* c)
{
    csilk_cors_config_t cors = {0};
    const char*         origin = config_env_get("CORS_ORIGIN", NULL, 0, NULL);
    if (origin && origin[0]) {
        cors.allow_origin = origin;
        cors.allow_credentials = 1;
    } else {
        const char* req_origin = csilk_get_header(c, "Origin");
        cors.allow_origin = req_origin && req_origin[0] ? req_origin : "*";
        cors.allow_credentials = 0;
    }
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token,X-Ledger-Id";
    csilk_cors_middleware(c, &cors);
}

/**
 * @brief 显式 OPTIONS 预检请求处理函数
 *
 * 用于路由中显式注册的 OPTIONS 路由节点，完成跨域握手。
 *
 * @param[in,out] c HTTP 请求上下文
 */
void
cors_preflight_handler(csilk_ctx_t* c)
{
    csilk_cors_config_t cors = {0};
    const char*         origin = config_env_get("CORS_ORIGIN", NULL, 0, NULL);
    if (!origin || !origin[0]) {
        const char* req_origin = csilk_get_header(c, "Origin");
        cors.allow_origin = req_origin && req_origin[0] ? req_origin : "*";
    } else {
        cors.allow_origin = origin;
    }
    cors.allow_methods = "GET,POST,PUT,DELETE,OPTIONS";
    cors.allow_headers = "Content-Type,Authorization,X-CSRF-Token,X-Ledger-Id";
    cors.allow_credentials = 1;
    csilk_cors_middleware(c, &cors);
}
