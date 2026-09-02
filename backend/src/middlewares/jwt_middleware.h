#pragma once

/**
 * @file jwt_middleware.h
 * @brief JWT 身份认证与 Token 状态校验中间件接口
 *
 * 负责解析 HTTP 请求中的 Bearer JWT 令牌、验证签名及有效期限，
 * 并与数据库中的用户 token_version 比对以实现即时失效吊销检测。
 */

#include "csilk/csilk.h"

/**
 * @brief 全局 JWT 身份认证中间件包装函数
 *
 * 拦截请求执行鉴权流程：
 * 1. 检查路由是否属于免认证公开白名单（如登录、注册、系统状态、公钥获取、2FA 登录验证、OAuth 回调等）。
 * 2. 验证环境变量 MINEFOLIO_JWT_SECRET 是否配置。
 * 3. 从 Authorization 请求头中提取 "Bearer <token>" 并验证 HS256 签名与 exp 过期时间。
 * 4. 解析成功后将 jwt_payload 存入请求上下文，并提取 user_id。
 * 5. 查询数据库校验 token 中的版本号 (v) 是否与 users 表中的 token_version 匹配。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 鉴权失败时直接返回 401 Unauthorized 或 500 Internal Server Error 并中断管道执行（csilk_abort）。
 * @note 线程安全性：线程安全。
 */
void jwt_middleware_wrapper(csilk_ctx_t* c);
