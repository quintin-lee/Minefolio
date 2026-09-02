/**
 * @file auth_controller.h
 * @brief 用户认证、授权、RSA-OAEP 传输加密与 TOTP 2FA 控制器头文件
 *
 * 声明用户登录、注册、修改密码、公钥获取、个人信息查询、TOTP 双因子认证
 * 以及 OAuth2/OIDC 单点登录相关的 HTTP 请求处理函数和路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 系统状态查询接口（向后兼容声明）
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void system_status(csilk_ctx_t* c);

/**
 * @brief 系统初始化引导接口（向后兼容声明）
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void system_setup(csilk_ctx_t* c);

/**
 * @brief 处理首个管理员注册请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/register
 *          鉴权要求: 公开访问（仅限系统未初始化时注册首个管理员，受频控中间件保护）
 *          请求体 (JSON):
 *          - username: 用户名 (string, 长度 >= 2)
 *          - password_enc: RSA-2048 加密的密码密文 (string, Base64URL 编码)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"token": "...", "expires_in": 604800}}
 *          - 400 Bad Request: 用户名或密码长度不合规、解密失败 (code: 1002)
 *          - 403 Forbidden: 系统已存在用户禁止重复注册 (code: 1004)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_register(csilk_ctx_t* c);

/**
 * @brief 处理用户登录认证请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/login
 *          鉴权要求: 公开访问（受频控中间件保护）
 *          请求体 (JSON):
 *          - username: 用户名 (string)
 *          - password_enc: RSA-2048 公钥加密的密码密文 (string, Base64URL 编码)
 *          - password: 明文密码 (string, 仅当未启用前端加密时的兼容备选)
 *          返回包格式:
 *          - 登录成功 (未开启 2FA):
 *            200 OK: {"code": 0, "message": "ok", "data": {"token": "...", "expires_in": 604800}}
 *          - 需二次验证 (已开启 2FA):
 *            200 OK: {"code": 0, "message": "ok", "data": {"require_2fa": true, "temp_token": "..."}}
 *          - 400 Bad Request: 缺少参数或密码解密失败 (code: 1002)
 *          - 401 Unauthorized: 用户不存在或密码错误 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_login(csilk_ctx_t* c);

/**
 * @brief 获取用于前端密码加密的 RSA-2048 公钥
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/auth/public-key
 *          鉴权要求: 公开访问
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"public_key": "-----BEGIN PUBLIC KEY-----\n..."}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_public_key(csilk_ctx_t* c);

/**
 * @brief 获取当前登录用户个人资料
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/auth/me
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, "username": "admin", "created_at": "..."}}
 *          - 401 Unauthorized: JWT 缺失或已过期 (code: 1001)
 *          - 404 Not Found: 用户不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_me(csilk_ctx_t* c);

/**
 * @brief 修改当前登录用户密码
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/auth/password
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - old_password_enc: RSA 加密的原密码 (string)
 *          - new_password_enc: RSA 加密的新密码 (string, 解密后长度 >= 6)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "密码修改成功", "data": null}
 *          - 400 Bad Request: 原密码错误或新密码不合规 (code: 1002)
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 *          成功修改密码后会自动递增 token_version，立即使所有历史已颁发的 JWT Token 失效。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_change_password(csilk_ctx_t* c);

/**
 * @brief 查询当前用户 TOTP 双因子认证开启状态
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/auth/2fa/status
 *          鉴权要求: JWT 认证
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"enabled": true, "has_backup_codes": true, "backup_codes_count": 8}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_2fa_status(csilk_ctx_t* c);

/**
 * @brief 初始化 TOTP 绑定配置
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/2fa/setup
 *          鉴权要求: JWT 认证
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"secret": "BASE32...", "otpauth_url": "otpauth://totp/..."}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_2fa_setup(csilk_ctx_t* c);

/**
 * @brief 校验动态码并正式启用 TOTP 2FA
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/2fa/enable
 *          鉴权要求: JWT 认证
 *          请求体 (JSON):
 *          - code: 6位 TOTP 动态验证码 (string)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"backup_codes": ["code1", "code2", ...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_2fa_enable(csilk_ctx_t* c);

/**
 * @brief 停用当前用户的 TOTP 双因子认证
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/2fa/disable
 *          鉴权要求: JWT 认证
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_2fa_disable(csilk_ctx_t* c);

/**
 * @brief TOTP 2FA 登录二次验证
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/2fa/verify-login
 *          鉴权要求: 公开访问（需携带第一阶段颁发的 temp_token）
 *          请求体 (JSON):
 *          - temp_token: 第一阶段登录返回的临时 Token (string)
 *          - code: 6位 TOTP 动态验证码或备用应急码 (string)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"token": "...", "expires_in": 604800}}
 *          - 400 Bad Request: 验证码错误或备用码已失效 (code: 1002)
 *          - 401 Unauthorized: temp_token 无效或已过期 (code: 1001)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_2fa_verify_login(csilk_ctx_t* c);

/**
 * @brief 获取已配置启用的 OAuth2 / OIDC 单点登录服务商列表
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/auth/oauth/providers
 *          鉴权要求: 公开访问
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"providers": [{"id": "github", "name": "GitHub", "icon": "...", "auth_url": "..."}, ...]}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_oauth_providers(csilk_ctx_t* c);

/**
 * @brief 处理 OAuth2 / OIDC 授权回调
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/auth/oauth/callback
 *          鉴权要求: 公开访问
 *          请求体 (JSON):
 *          - provider: 服务商标识 (string, 如 "github" / "oidc")
 *          - code: 授权码 (string)
 *          - oauth_id: 外部唯一用户标识 (string, 可选)
 *          - username: 建议用户名 (string, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"token": "...", "expires_in": 604800, "user": {"id": 1, "username": "...", "oauth_provider": "..."}}}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void auth_oauth_callback(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册认证与授权相关的所有 REST 路由
 *
 * @details 注册包括登录、注册、公钥获取、修改密码、2FA 流程及 OAuth 回调等端点。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_auth_routes(csilk_app_t* app);
