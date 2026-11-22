/**
 * @file csrf_middleware.c
 * @brief 双重 Cookie 跨站请求伪造 (CSRF) 防护中间件实现
 *
 * 实现了基于 Double Submit Cookie 策略的 CSRF 令牌生成、Cookie 设置及写操作防伪验证。
 */

#include "middlewares/csrf_middleware.h"
#include "csilk/core/ctx/response.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char* s_csrf_exclude_paths[] = {
    "/api/auth/login", "/api/auth/register", "/api/system/status", "/api/system/setup", NULL};

static bool
is_csrf_excluded_path(const char* path)
{
    if (!path) {
        return false;
    }
    for (int i = 0; s_csrf_exclude_paths[i]; i++) {
        if (strcmp(path, s_csrf_exclude_paths[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool
is_safe_http_method(const char* method)
{
    return method && (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
                      strcmp(method, "OPTIONS") == 0);
}

static int
csrf_should_set_secure(const csilk_ctx_t* c)
{
    const char* env_sec = getenv("MINEFOLIO_COOKIE_SECURE");
    if (env_sec && env_sec[0]) {
        if (strcmp(env_sec, "1") == 0 || strcasecmp(env_sec, "true") == 0 ||
            strcasecmp(env_sec, "yes") == 0) {
            return 1;
        }
        if (strcmp(env_sec, "0") == 0 || strcasecmp(env_sec, "false") == 0 ||
            strcasecmp(env_sec, "no") == 0) {
            return 0;
        }
    }

    const char* xf_proto = csilk_get_header((csilk_ctx_t*)c, "X-Forwarded-Proto");
    if (xf_proto && strcasecmp(xf_proto, "https") == 0) {
        return 1;
    }

    const char* xf_ssl = csilk_get_header((csilk_ctx_t*)c, "X-Forwarded-Ssl");
    if (xf_ssl && strcasecmp(xf_ssl, "on") == 0) {
        return 1;
    }

    const char* forwarded = csilk_get_header((csilk_ctx_t*)c, "Forwarded");
    if (forwarded) {
        const char* p = forwarded;
        while (*p) {
            if (strncasecmp(p, "proto=https", 11) == 0) {
                char next = p[11];
                if (next == '\0' || next == ';' || next == ',' || next == ' ') {
                    return 1;
                }
            }
            p++;
        }
    }

    return 0;
}

static bool
csrf_skip_if_bearer_or_seed(csilk_ctx_t* c)
{
    const char* cookie = csilk_get_cookie(c, "csrf_token");
    if (!cookie || !cookie[0]) {
        /* 若携带了 Bearer 鉴权头，直接放行，避免 API/移动端客户端因缺少 cookie 被拦截 */
        const char* auth_hdr = csilk_get_header(c, "Authorization");
        if (auth_hdr && strncmp(auth_hdr, "Bearer ", 7) == 0) {
            return true;
        }
        /* 仅对安全方法或初始化/登录免检路径下发 csrf_token Cookie，避免在 403 拒绝响应中签发令牌 */
        const char* method = csilk_get_method(c);
        const char* path = csilk_get_path(c);
        if (is_safe_http_method(method) || is_csrf_excluded_path(path)) {
            char buf[33];
            if (csilk_csrf_generate_token(buf, sizeof(buf)) == 0) {
                int secure = csrf_should_set_secure(c);
                csilk_set_cookie_ex(c, "csrf_token", buf, 86400, "/", NULL, secure, 0, "Lax");
            }
            OPENSSL_cleanse(buf, sizeof(buf));
        }
    }
    return false;
}

/**
 * @brief CSRF 防护中间件包装实现函数
 *
 * 委托底层 csilk_csrf_middleware_ext 执行双重 Cookie 校验，配置白名单与 Bearer 自动放行。
 *
 * @param[in,out] c HTTP 上下文对象指针
 */
void
csrf_middleware_wrapper(csilk_ctx_t* c)
{
    static const csilk_csrf_config_t cfg = {
        .cookie_name = "csrf_token",
        .header_name = "X-CSRF-Token",
        .exclude_paths = s_csrf_exclude_paths,
        .skip_fn = csrf_skip_if_bearer_or_seed,
    };
    csilk_csrf_middleware_ext(c, &cfg);
}
