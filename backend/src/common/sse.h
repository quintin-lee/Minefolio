#pragma once

/**
 * @file sse.h
 * @brief Minefolio SSE 流式响应封装（保留 CORS 与安全中间件响应头）
 *
 * csilk 原生 csilk_sse_init() 直接向 socket 写入硬编码响应头，会绕过并丢弃
 * c->response.headers 中由 CORS / Security Headers 中间件注入的头部，导致移动端
 * Capacitor WebView 或跨域浏览器调用 SSE 接口时被 CORS 策略拦截。
 * 本模块基于 csilk_response_write() / csilk_response_end() 实现分块流式 SSE，
 * 确保完整携带全部已配置的响应头，并在流结束后规范输出终端块。
 */

#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SSE 流式响应（HTTP 200 + text/event-stream + 全量中间件响应头）
 * @param[in,out] c 请求上下文
 * @return >0 正常就绪，0 触发背压，<0 发送失败或上下文无效
 */
int mf_sse_init(csilk_ctx_t* c);

/**
 * @brief 发送单条 SSE 事件或心跳注释帧
 * @param[in,out] c     请求上下文
 * @param[in]     event 事件名（NULL 且 data 为 NULL 时发送心跳注释）
 * @param[in]     data  事件载荷字符串
 * @return 1 正常，0 背压，<0 发送失败/连接已关闭
 */
int mf_sse_send(csilk_ctx_t* c, const char* event, const char* data);

/**
 * @brief 结束 SSE 分块响应并释放连接状态
 * @param[in,out] c 请求上下文
 */
void mf_sse_end(csilk_ctx_t* c);

#ifdef __cplusplus
}
#endif
