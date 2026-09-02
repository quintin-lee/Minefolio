#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @file auth_service.h
 * @brief 用户认证、授权、RSA-OAEP 端到端传输加密、TOTP 2FA 及 OAuth2/OIDC SSO 业务逻辑服务
 */

/**
 * @brief 处理用户注册请求 (POST /api/auth/register)
 * @param c HTTP 上下文
 */
void auth_register(csilk_ctx_t* c);

/**
 * @brief 处理用户登录认证 (POST /api/auth/login)
 * 支持 RSA-OAEP 密文密码自动解密与 TOTP 2FA 登录两步验证拦截
 * @param c HTTP 上下文
 */
void auth_login(csilk_ctx_t* c);

/**
 * @brief 修改当前登录用户密码 (POST /api/auth/change-password)
 * @param c HTTP 上下文
 */
void auth_change_password(csilk_ctx_t* c);

/**
 * @brief 获取当前登录用户信息与基础权限 (GET /api/auth/me)
 * @param c HTTP 上下文
 */
void auth_me(csilk_ctx_t* c);

/**
 * @brief 获取用于客户端传输密码加密的 RSA-2048 公钥 (GET /api/auth/public-key)
 * @param c HTTP 上下文
 */
void auth_public_key(csilk_ctx_t* c);

/**
 * @brief 查询当前用户的 TOTP 双因子认证开启状态 (GET /api/auth/2fa/status)
 * @param c HTTP 上下文
 */
void auth_2fa_status(csilk_ctx_t* c);

/**
 * @brief 初始化 TOTP 双因子绑定，生成 Base32 密钥与 otpauth:// 二维码 URL (POST /api/auth/2fa/setup)
 * @param c HTTP 上下文
 */
void auth_2fa_setup(csilk_ctx_t* c);

/**
 * @brief 校验 TOTP 6 位动态验证码并正式启用 2FA (POST /api/auth/2fa/enable)
 * @param c HTTP 上下文
 */
void auth_2fa_enable(csilk_ctx_t* c);

/**
 * @brief 验证密码/TOTP 后停用 2FA (POST /api/auth/2fa/disable)
 * @param c HTTP 上下文
 */
void auth_2fa_disable(csilk_ctx_t* c);

/**
 * @brief 登录二次验证：使用 2fa_temp_token 与 6 位 TOTP 动态码换取正式 JWT Token (POST /api/auth/2fa/verify-login)
 * @param c HTTP 上下文
 */
void auth_2fa_verify_login(csilk_ctx_t* c);

/**
 * @brief 获取当前系统已配置启用的 OAuth2 / OIDC 单点登录服务商列表 (GET /api/auth/oauth/providers)
 * @param c HTTP 上下文
 */
void auth_oauth_providers(csilk_ctx_t* c);

/**
 * @brief 处理 OAuth2 / OIDC 授权回调换取用户信息并自动发码登录 (POST /api/auth/oauth/callback)
 * @param c HTTP 上下文
 */
void auth_oauth_callback(csilk_ctx_t* c);

/**
 * @brief 注册认证模块相关的 RESTful 路由
 * @param app Csilk 应用实例
 */
void register_auth_routes(csilk_app_t* app);
