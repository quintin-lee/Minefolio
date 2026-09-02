/**
 * @file jwt.c
 * @brief JWT 令牌生成与用户信息提取工具实现
 *
 * 实现了基于 HS256 与 HMAC-SHA256 的 JWT 令牌签名签发，
 * 以及非破坏性提取请求上下文 JWT 用户载荷属性的逻辑。
 */

#include "jwt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief 读取 MINEFOLIO_JWT_SECRET 环境变量
 *
 * @return const char* 密钥字符串指针，未设置返回 NULL
 */
static const char*
jwt_secret(void)
{
    return getenv("MINEFOLIO_JWT_SECRET");
}

/**
 * @brief 生成携带用户身份与版本号的 JWT 字符串
 *
 * @param[in,out] c HTTP 请求上下文
 * @param[in] user_id 用户 ID
 * @param[in] token_version Token 版本号
 *
 * @return char* 生成的 JWT 字符串指针，需由调用者 free()
 */
char*
jwt_generate_token(csilk_ctx_t* c, int64_t user_id, int token_version)
{
    csilk_json_t* payload = csilk_json_object();
    int64_t       now = (int64_t)time(NULL);
    csilk_json_add_int(payload, "sub", user_id);
    csilk_json_add_int(payload, "iat", now);
    csilk_json_add_int(payload, "exp", now + 604800); /* 7 天有效 */
    csilk_json_add_int(payload, "v", token_version);  /* 版本号用于批量注销 */

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

/**
 * @brief 从上下文中安全获取用户 ID
 *
 * @param[in,out] c HTTP 请求上下文
 * @return int64_t 用户 ID，失败返回 -1
 */
int64_t
jwt_get_user_id(csilk_ctx_t* c)
{
    /* 读取 JWT 载荷指针而不消耗它 — 避免后续验证失效 */
    csilk_json_t* payload = (csilk_json_t*)csilk_get(c, "jwt_payload");
    if (!payload) {
        return -1;
    }

    int64_t uid = (int64_t)csilk_json_get_number(payload, "sub");
    return uid;
}
