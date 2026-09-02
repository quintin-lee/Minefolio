#pragma once

/**
 * @file csrf_middleware.h
 * @brief 双重 Cookie 跨站请求伪造 (CSRF) 防护中间件接口
 *
 * 通过双重 Cookie 提交模式（Double Submit Cookie）提供 CSRF 防护，
 * 对写操作强制要求 X-CSRF-Token 请求头与 csrf_token Cookie 保持一致。
 */

#include "csilk/csilk.h"

/**
 * @brief CSRF 防护中间件处理函数
 *
 * 拦截所有 HTTP 请求并执行 CSRF 防护逻辑：
 * 1. 安全方法（GET, HEAD, OPTIONS）：若客户端缺失 csrf_token Cookie，则自动生成并注入 Set-Cookie 响应头。
 * 2. 免检路径（如 /api/auth/login, /api/auth/register, /api/system/status, /api/system/setup）：跳过校验并确保植入 Cookie。
 * 3. 状态变更方法（POST, PUT, DELETE）：校验 X-CSRF-Token 请求头与 csrf_token Cookie 的一致性；
 *    若 Cookie 缺失但携带合法 Bearer Token，则予以放行并下发新 CSRF Cookie。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 校验失败时将直接向客户端发送 403 Forbidden 错误并中断后续中间件管道执行（csilk_abort）。
 * @note 线程安全性：线程安全。
 */
void csrf_middleware_wrapper(csilk_ctx_t* c);
