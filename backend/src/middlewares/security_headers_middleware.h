#pragma once

/**
 * @file security_headers_middleware.h
 * @brief HTTP 基础安全响应头注入中间件接口
 *
 * 为服务端返回的所有 HTTP 响应自动注入标准安全防护响应头，
 * 包括点击劫持防御、MIME 类型混淆防御及跨域来源引用策略配置。
 */

#include "csilk/csilk.h"

/**
 * @brief 安全响应头中间件处理函数
 *
 * 全局应用于所有 HTTP 请求，在响应中自动附加以下安全防护标头：
 * - X-Frame-Options: DENY（禁止在 frame/iframe/object 中嵌入页面，防止点击劫持）
 * - X-Content-Type-Options: nosniff（禁止浏览器进行 MIME 类型探测嗅探）
 * - Referrer-Policy: strict-origin-when-cross-origin（同源请求发送完整 URL，跨域请求仅发送源信息）
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @note 线程安全性：线程安全。
 */
void security_headers_middleware(csilk_ctx_t* c);
