#pragma once

/**
 * @file response.h
 * @brief HTTP 统一 JSON 响应封装与分页参数解析工具
 *
 * 统一后端所有 API 的 JSON 响应格式 {code, message, data}，
 * 并提供常用业务状态码（成功、未授权、参数错误、未找到、冲突等）的响应构造函数，
 * 以及 URL 分页参数解析工具。
 */

#include "csilk/csilk.h"
#include <stdlib.h>

/**
 * @brief 构造并返回标准成功 JSON 响应 (code: 0)
 *
 * 响应体格式：{"code": 0, "message": "ok", "data": <data>}
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] data 业务数据 JSON 对象指针，可为 NULL 或任意 JSON 节点
 *
 * @note 内存所有权：data 节点将被挂载到响应根对象 r 中，由 csilk_json() 最终托管并释放。
 */
static inline void
respond_ok(csilk_ctx_t* c, csilk_json_t* data)
{
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "code", 0);
    csilk_json_add_string(r, "message", "ok");
    csilk_json_add_object(r, "data", data);
    csilk_json(c, CSILK_STATUS_OK, r);
}

/**
 * @brief 构造并返回不含业务数据的成功响应 (code: 0, data: null)
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 */
static inline void
respond_ok_null(csilk_ctx_t* c)
{
    respond_ok(c, csilk_json_null());
}

/**
 * @brief 构造并返回自定义业务错误码的 JSON 响应
 *
 * 响应体格式：{"code": <code>, "message": <msg>}
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] code 自定义业务状态码（如 1001, 1002, 1003, 1004 等）
 * @param[in] msg 错误描述文本
 */
static inline void
respond_error(csilk_ctx_t* c, int code, const char* msg)
{
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "code", code);
    csilk_json_add_string(r, "message", msg);
    csilk_json(c, CSILK_STATUS_OK, r);
}

/**
 * @brief 构造并返回未授权错误响应 (code: 1001, "未授权")
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 */
static inline void
respond_unauthorized(csilk_ctx_t* c)
{
    respond_error(c, 1001, "未授权");
}

/**
 * @brief 构造并返回请求参数错误响应 (code: 1002)
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] msg 错误提示说明；若为 NULL 则默认为 "参数错误"
 */
static inline void
respond_bad_request(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1002, msg ? msg : "参数错误");
}

/**
 * @brief 构造并返回资源不存在错误响应 (code: 1003, "资源不存在")
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 */
static inline void
respond_not_found(csilk_ctx_t* c)
{
    respond_error(c, 1003, "资源不存在");
}

/**
 * @brief 构造并返回资源冲突/已存在错误响应 (code: 1004)
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] msg 错误提示说明；若为 NULL 则默认为 "资源已存在"
 */
static inline void
respond_conflict(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1004, msg ? msg : "资源已存在");
}

/**
 * @brief 构造并返回权限受限/操作被禁止响应 (code: 2001)
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] msg 错误提示说明；若为 NULL 则默认为 "操作被禁止"
 */
static inline void
respond_forbidden(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 2001, msg ? msg : "操作被禁止");
}

/**
 * @brief 从 HTTP Query 参数中解析分页参数 (page, page_size)
 *
 * 默认值规则：
 *   - page 默认为 1，且最小值限制为 1。
 *   - page_size 默认为 20，最小值限制为 1，最大值上限截断为 500。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[out] page 输出解析后的当前页码
 * @param[out] page_size 输出解析后的每页数量
 */
static inline void
parse_page_params(csilk_ctx_t* c, int64_t* page, int64_t* page_size)
{
    const char* page_str = csilk_get_query(c, "page");
    const char* size_str = csilk_get_query(c, "page_size");
    int64_t     p = 1, s = 20;
    if (page_str) {
        p = atoll(page_str);
        if (p < 1) {
            p = 1;
        }
    }
    if (size_str) {
        s = atoll(size_str);
        if (s < 1) {
            s = 20;
        }
        if (s > 500) {
            s = 500;
        }
    }
    *page = p;
    *page_size = s;
}

/**
 * @brief 构造并返回分页列表成功响应
 *
 * 封装数据结构：
 *   {
 *     "code": 0,
 *     "message": "ok",
 *     "data": {
 *       "list": [...],
 *       "total": <total>,
 *       "page": <page>,
 *       "page_size": <page_size>
 *     }
 *   }
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] list 数据列表 JSON 数组指针
 * @param[in] total 记录总条数
 * @param[in] page 当前页码
 * @param[in] page_size 每页数量
 *
 * @note 内存所有权：list 数组由生成的 data 对象接管，最终由 csilk 自动释放。
 */
static inline void
respond_page_ok(csilk_ctx_t* c, csilk_json_t* list, int64_t total, int64_t page, int64_t page_size)
{
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_array(data, "list", list);
    csilk_json_add_number(data, "total", (double)total);
    csilk_json_add_number(data, "page", (double)page);
    csilk_json_add_number(data, "page_size", (double)page_size);
    respond_ok(c, data);
}
