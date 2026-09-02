/**
 * @file csrf_middleware.c
 * @brief 双重 Cookie 跨站请求伪造 (CSRF) 防护中间件实现
 *
 * 实现了基于 Double Submit Cookie 策略的 CSRF 令牌生成、Cookie 设置及写操作防伪验证。
 */

#include "middlewares/csrf_middleware.h"
#include "csilk/core/response.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief CSRF 防护中间件包装实现函数
 *
 * 针对安全请求注入 Cookie，针对修改类请求校验 X-CSRF-Token 与 Cookie 的值。
 *
 * @param[in,out] c HTTP 上下文对象指针
 */
void
csrf_middleware_wrapper(csilk_ctx_t* c)
{
    const char* method = csilk_get_method(c);
    if (!method) {
        csilk_next(c);
        return;
    }

    /* 对安全方法 (GET/HEAD/OPTIONS)，确保客户端持有 csrf_token cookie */
    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
        strcmp(method, "OPTIONS") == 0) {
        if (!csilk_get_cookie(c, "csrf_token")) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
            }
        }
        csilk_next(c);
        return;
    }

    /* 对写方法 (POST/PUT/DELETE)：检查白名单免检路径 */
    const char* path = csilk_get_path(c);
    if (path &&
        (strcmp(path, "/api/auth/login") == 0 || strcmp(path, "/api/auth/register") == 0 ||
         strcmp(path, "/api/system/status") == 0 || strcmp(path, "/api/system/setup") == 0)) {
        if (!csilk_get_cookie(c, "csrf_token")) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
            }
        }
        csilk_next(c);
        return;
    }

    /* 校验 CSRF Token */
    const char* token = csilk_get_header(c, "X-CSRF-Token");
    const char* cookie = csilk_get_cookie(c, "csrf_token");

    if (cookie && cookie[0]) {
        /* 当 cookie 存在时，X-CSRF-Token 请求头必须与 cookie 值严格匹配 */
        if (!token || strcmp(token, cookie) != 0) {
            csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: Invalid CSRF token");
            csilk_abort(c);
            return;
        }
    } else {
        /* 若尚无 CSRF cookie，仅在请求携带 Bearer 鉴权头时放行 */
        const char* auth_hdr = csilk_get_header(c, "Authorization");
        if (!auth_hdr || strncmp(auth_hdr, "Bearer ", 7) != 0) {
            csilk_json_error(c, CSILK_STATUS_FORBIDDEN, "Forbidden: CSRF cookie missing");
            csilk_abort(c);
            return;
        }
        /* 为后续请求下发 CSRF cookie */
        char buf[33];
        if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
            csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
        }
    }
    csilk_next(c);
}
