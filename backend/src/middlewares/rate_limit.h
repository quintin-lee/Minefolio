#pragma once

/**
 * @file rate_limit.h
 * @brief 认证相关敏感接口请求频率限制（限流）中间件接口
 *
 * 针对登录、注册、系统初始配置、2FA 验证等关键认证写接口，
 * 提供基于内存滑动窗口环形缓冲区的访问频次控制，防范暴力破解与 DoS 攻击。
 */

#include "csilk/csilk.h"

/**
 * @brief 认证敏感接口限流中间件处理函数
 *
 * 拦截请求并对以下路径实施滑动窗口限流：
 *   - /api/auth/login
 *   - /api/auth/register
 *   - /api/system/setup
 *   - /api/auth/2fa/verify-login
 *
 * 限制规则：
 *   - 窗口大小：60 秒 (RATE_LIMIT_WINDOW_SEC)
 *   - 最大允许请求数：10 次 (RATE_LIMIT_MAX_REQS)
 *   - 限制维度：按客户端 IP + 请求 Path 组合统计
 *
 * 超出限制时返回 HTTP 429 Too Many Requests，响应体错误码 1004，并附加 "Retry-After: 60" 响应头。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 线程安全性：内部使用 pthread_mutex_t 互斥锁保护环形缓冲区，线程安全。
 */
void rate_limit_auth_middleware(csilk_ctx_t* c);

/**
 * @brief MCP 端点 (/mcp) 每用户限流中间件
 *
 * 拦截 POST /mcp（streamable-HTTP JSON-RPC 端点），按 user_id（来自 JWT，
 * 中间件注册在 JWT 组之后）实施独立滑动窗口限频，防范单用户 hammering
 * tools/call 触发内置写操作。与 rate_limit_auth_middleware（按 IP+path）
 * 完全隔离：独立环形缓冲区、独立互斥锁、独立上限（120 次/分）。
 *
 * 超出限制时返回 HTTP 429 + JSON-RPC 错误帧（{"jsonrpc","id":0,"error":{code:-32005,...}}），
 * 并附加 "Retry-After: 60" 响应头。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 线程安全性：内部使用 pthread_mutex_t 互斥锁保护环形缓冲区，线程安全。
 */
void mcp_rate_limit_middleware(csilk_ctx_t* c);
