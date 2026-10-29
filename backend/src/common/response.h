#pragma once

/**
 * @file response.h
 * @brief HTTP JSON response and pagination helpers (delegating to csilk core).
 *
 * Provides standardized JSON response formatting {code, message, data} and
 * pagination query parameter extraction. Delegates to csilk's downstreamed
 * response envelope subsystem while preserving Minefolio's legacy conventions
 * (HTTP 200 with business code envelope, and "list" pagination array key).
 */

#include "csilk/csilk.h"
#include "csilk/core/ctx/response.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Construct and return standard success JSON response (code: 0).
 *
 * Response format: {"code": 0, "message": "ok", "data": <data>}.
 * Takes ownership of data node.
 *
 * @param[in,out] c    HTTP request context.
 * @param[in]     data Business JSON payload node (transfers ownership).
 */
static inline void
respond_ok(csilk_ctx_t* c, csilk_json_t* data)
{
    csilk_respond_ok(c, data);
}

/**
 * @brief Construct and return success response without data (code: 0, data: null).
 *
 * @param[in,out] c HTTP request context.
 */
static inline void
respond_ok_null(csilk_ctx_t* c)
{
    csilk_respond_ok_null(c);
}

/**
 * @brief Construct and return JSON response with custom business code.
 *
 * Minefolio legacy convention: returns HTTP 200 with {"code": <code>, "message": <msg>}.
 *
 * @param[in,out] c    HTTP request context.
 * @param[in]     code Custom business status code (e.g., 1001, 1002, 1003, 1004).
 * @param[in]     msg  Error description message.
 */
static inline void
respond_error(csilk_ctx_t* c, int code, const char* msg)
{
    csilk_respond_fail(c, CSILK_STATUS_OK, code, msg);
}

/**
 * @brief Construct and return unauthorized error response (code: 1001, "未授权").
 *
 * @param[in,out] c HTTP request context.
 */
static inline void
respond_unauthorized(csilk_ctx_t* c)
{
    respond_error(c, 1001, "未授权");
}

/**
 * @brief Construct and return bad request parameter error response (code: 1002).
 *
 * @param[in,out] c   HTTP request context.
 * @param[in]     msg Error description (defaults to "参数错误" if NULL).
 */
static inline void
respond_bad_request(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1002, msg ? msg : "参数错误");
}

/**
 * @brief Construct and return not found error response (code: 1003, "资源不存在").
 *
 * @param[in,out] c HTTP request context.
 */
static inline void
respond_not_found(csilk_ctx_t* c)
{
    respond_error(c, 1003, "资源不存在");
}

/**
 * @brief Construct and return conflict error response (code: 1004).
 *
 * @param[in,out] c   HTTP request context.
 * @param[in]     msg Error description (defaults to "资源已存在" if NULL).
 */
static inline void
respond_conflict(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1004, msg ? msg : "资源已存在");
}

/**
 * @brief Construct and return forbidden error response (code: 2001).
 *
 * @param[in,out] c   HTTP request context.
 * @param[in]     msg Error description (defaults to "操作被禁止" if NULL).
 */
static inline void
respond_forbidden(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 2001, msg ? msg : "操作被禁止");
}

/**
 * @brief Strictly parse integer query parameter with strtoll and bounds check.
 *
 * @param[in] s    Query string to parse.
 * @param[in] dflt Fallback default value on failure or overflow.
 * @return Parsed integer or dflt.
 */
static inline int64_t
parse_query_int_strict(const char* s, int64_t dflt)
{
    if (!s || *s == '\0') {
        return dflt;
    }
    errno = 0;
    char*     end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || end == s) {
        return dflt;
    }
    if (*end != '\0') {
        if (*end == '.') {
            const char* p = end + 1;
            if (*p == '\0') {
                return dflt;
            }
            while (*p == '0') {
                p++;
            }
            if (*p != '\0') {
                return dflt;
            }
        } else {
            return dflt;
        }
    }
    return (int64_t)v;
}

/**
 * @brief Parse pagination parameters (page, page_size) from HTTP query string.
 *
 * Default rules:
 *   - page defaults to 1, clamped to >= 1.
 *   - page_size defaults to 20, clamped to [1, 500].
 *
 * @param[in,out] c         HTTP request context.
 * @param[out]    page      Parsed page number output pointer.
 * @param[out]    page_size Parsed page size output pointer.
 */
static inline void
parse_page_params(csilk_ctx_t* c, int64_t* page, int64_t* page_size)
{
    csilk_parse_pagination(c, page, page_size, 20, 500);
}

/**
 * @brief Construct and return paginated list success response.
 *
 * Preserves the "list" array key expected by Minefolio's Vue 3 frontend:
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
 * @param[in,out] c         HTTP request context.
 * @param[in]     list      List items JSON array (transfers ownership).
 * @param[in]     total     Total record count.
 * @param[in]     page      Current page number.
 * @param[in]     page_size Page size.
 */
static inline void
respond_page_ok(csilk_ctx_t* c, csilk_json_t* list, int64_t total, int64_t page, int64_t page_size)
{
    csilk_respond_page_ex(c, "list", list, total, page, page_size);
}
