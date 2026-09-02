#pragma once

/**
 * @file cors_middleware.h
 * @brief 跨域资源共享 (CORS) 中间件与预检请求处理接口
 *
 * 提供标准 CORS 响应头的自动注入及 OPTIONS 预检请求的独立处理，
 * 支持通过环境变量动态配置允许的源（Origin）及凭证携带策略。
 */

#include "csilk/csilk.h"

/**
 * @brief 全局 CORS 中间件包装器
 *
 * 拦截传入的 HTTP 请求，根据环境变量 MINEFOLIO_CORS_ORIGIN 或请求中的 Origin 请求头，
 * 动态配置 Access-Control-Allow-* 响应头并调用底层 csilk CORS 管道。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 环境变量支持：
 *   - MINEFOLIO_CORS_ORIGIN: 若设置非空，则固定允许此源并开启凭据（Allow-Credentials: true）；
 *     若未设置，则默认反射请求中的 Origin 头或回退为 "*"，并禁用凭据。
 * @note 允许的 HTTP 方法：GET, POST, PUT, DELETE, OPTIONS。
 * @note 允许的 HTTP 头部：Content-Type, Authorization, X-CSRF-Token, X-Ledger-Id。
 * @note 线程安全性：线程安全，仅读取环境变量及请求上下文。
 */
void cors_middleware_wrapper(csilk_ctx_t* c);

/**
 * @brief CORS OPTIONS 预检请求处理函数
 *
 * 专门用于响应 HTTP OPTIONS 预检请求，直接设置 CORS 相关头信息并完成请求生命周期。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 线程安全性：线程安全。
 */
void cors_preflight_handler(csilk_ctx_t* c);
