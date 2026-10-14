# Design Specification: RESTful Response Envelopes & Pagination Helpers Downstreaming

- **Date**: 2026-10-14
- **Status**: Approved
- **Target Repositories**: `csilk` (framework implementation) and `Minefolio` (application consumer)
- **Language & Comment Policy**: All code, Doxygen docstrings, and in-line comments must use **English**.

---

## 1. Background & Motivation

Minefolio previously implemented standard HTTP JSON response wrappers in `backend/src/common/response.h`, defining boilerplate helper functions for:
1. Standard success envelope: `{"code": 0, "message": "ok", "data": <data>}`
2. Semantic error envelopes: `{"code": <code>, "message": "<msg>"}`
3. Strict integer query parameter parsing and boundary validation (`strtoll` based)
4. Standard paginated responses: `{"code": 0, "message": "ok", "data": {"list": [...], "total": N, "page": P, "page_size": S}}`

Currently, `csilk` provides raw response primitives (`csilk_status`, `csilk_string`, `csilk_json`, `csilk_json_error`), but lacks high-level RESTful API response helpers and robust pagination query parsers. As a result, web services and controllers built on `csilk` repeatedly write redundant boilerplate to assemble JSON objects, handle HTTP status codes, and parse pagination query strings.

This specification details the downstreaming of unified RESTful response envelope functions into `csilk`'s core response subsystem, providing zero-heap fast-paths and backward-compatible integration with `Minefolio`.

---

## 2. Requirements & Design Decisions

### 2.1 Functional Requirements
- **Standard Success Envelope**: Produce HTTP 200 with payload `{"code": 0, "message": "ok", "data": <data>}`. If `data` is NULL, output `"data": null`.
- **Hybrid Error Handling**: Support setting both the HTTP status code (e.g. 400, 401, 403, 404, 409, 500) and an application-level business error code (`code`) alongside a human-readable `message`.
- **Convenience Error Helpers**: Provide semantic shortcuts for Bad Request (400), Unauthorized (401), Forbidden (403), Not Found (404), and Conflict (409). If `biz_code == 0`, default to the HTTP status code.
- **Strict Pagination Parsing**: Safely extract `page` and `page_size` from query strings with `strtoll`, preventing integer overflow and clamping values to `[1, max_size]` with fallback defaults.
- **OpenAPI & Standard Alignment**: Default paginated envelope uses `"items"` as the array field key (`{"items": [...], "total": N, "page": P, "page_size": S}`).
- **Custom Array Key Support**: Provide an extended pagination response function (`csilk_respond_page_ex`) allowing caller-specified array keys (e.g., `"list"` for Minefolio).
- **Code Language Convention**: All code comments and documentation MUST strictly be in English.

### 2.2 Non-Functional Requirements
- **Performance**: Optimize small error responses via stack buffer + Arena allocation (`csilk_arena_strndup`), avoiding heap allocation (`malloc`/`free`) on the error hot path.
- **Memory Safety**: `csilk_respond_ok` and `csilk_respond_page*` take full ownership of the provided `csilk_json_t*` tree. No leaks on early return or invalid context.
- **Zero Breaking Changes**: Minefolio's existing controllers and frontend Vue components must remain 100% compatible without modifying API routes or TypeScript types.

---

## 3. Architecture & Public Interface

### 3.1 Header Declarations (`csilk/include/csilk/core/response.h`)

```c
/* =========================================================================
 * RESTful API Response Envelopes & Pagination Helpers
 * ========================================================================= */

/**
 * @brief Send a standard success response (HTTP 200, code: 0).
 *
 * Produces {"code": 0, "message": "ok", "data": <data>}.
 * Takes ownership of the data JSON node. If data is NULL, sets "data": null.
 *
 * @param c    The request context (must not be NULL).
 * @param data The payload JSON element (ownership transferred to framework).
 */
void csilk_respond_ok(csilk_ctx_t* c, csilk_json_t* data);

/**
 * @brief Send an empty success response (HTTP 200, code: 0, data: null).
 *
 * @param c The request context.
 */
void csilk_respond_ok_null(csilk_ctx_t* c);

/**
 * @brief Send a structured business error response.
 *
 * Produces {"code": <biz_code>, "message": "<message>"}.
 *
 * @param c           The request context.
 * @param http_status HTTP status code (e.g., 400, 401, 404, 500).
 * @param biz_code    Application-defined error code (if 0, falls back to http_status).
 * @param message     Human-readable error description (if NULL, uses default status text).
 */
void csilk_respond_fail(csilk_ctx_t* c, int http_status, int biz_code, const char* message);

/* Semantic error helpers (uses default biz_code if biz_code == 0) */
void csilk_respond_bad_request(csilk_ctx_t* c, int biz_code, const char* msg);  /* 400 */
void csilk_respond_unauthorized(csilk_ctx_t* c, int biz_code, const char* msg); /* 401 */
void csilk_respond_forbidden(csilk_ctx_t* c, int biz_code, const char* msg);    /* 403 */
void csilk_respond_not_found(csilk_ctx_t* c, int biz_code, const char* msg);    /* 404 */
void csilk_respond_conflict(csilk_ctx_t* c, int biz_code, const char* msg);     /* 409 */

/**
 * @brief Strictly parse pagination parameters from query string (page, page_size).
 *
 * Safely parses with strtoll. Clamps page >= 1 (default 1).
 * Clamps page_size in [1, max_size] (default default_size).
 *
 * @param c            The request context.
 * @param out_page     Pointer to output page number.
 * @param out_page_size Pointer to output page size.
 * @param default_size Default page size if absent/invalid (<=0 defaults to 20).
 * @param max_size     Maximum allowed page size (<=0 defaults to 500).
 */
void csilk_parse_pagination(csilk_ctx_t* c,
                            int64_t*     out_page,
                            int64_t*     out_page_size,
                            int64_t      default_size,
                            int64_t      max_size);

/**
 * @brief Send a standard paginated response with "items" as the array key.
 *
 * Produces {"code": 0, "message": "ok", "data": {"items": [...], "total": N, "page": P, "page_size": S}}.
 * Takes ownership of items JSON array.
 *
 * @param c         The request context.
 * @param items     JSON array of records (ownership transferred; if NULL, uses []).
 * @param total     Total count of matching records.
 * @param page      Current page index (1-based).
 * @param page_size Page size.
 */
void csilk_respond_page(csilk_ctx_t* c,
                        csilk_json_t* items,
                        int64_t       total,
                        int64_t       page,
                        int64_t       page_size);

/**
 * @brief Send a paginated response with a custom array key (e.g. "list").
 *
 * @param c         The request context.
 * @param array_key The JSON key for the items array (defaults to "items" if NULL/empty).
 * @param items     JSON array of records (ownership transferred).
 * @param total     Total record count.
 * @param page      Current page.
 * @param page_size Page size.
 */
void csilk_respond_page_ex(csilk_ctx_t* c,
                           const char*   array_key,
                           csilk_json_t* items,
                           int64_t       total,
                           int64_t       page,
                           int64_t       page_size);
```

---

## 4. Implementation Details (`csilk/src/core/primitives/response.c`)

### 4.1 Error Hot Path (Stack Buffer & Arena)
For small error strings without control characters:
1. Format into a 512-byte stack buffer:
   `snprintf(buf, sizeof(buf), "{\"code\":%d,\"message\":\"%s\"}", code, msg);`
2. If `c->arena` is available, copy via `csilk_arena_strndup()` and set `c->response.body_ownership = CSILK_OWN_ARENA`.
3. If the message is long or contains escape-requiring characters (quotes, backslashes, newlines), fall back to `csilk_json_object()` to guarantee RFC 8259 compliance.

### 4.2 Query Strict Integer Parsing
Implement an internal helper:
```c
static int64_t
_csilk_parse_query_int_strict(const char* s, int64_t dflt)
{
    if (!s || *s == '\0') return dflt;
    errno = 0;
    char* end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return dflt;
    return (int64_t)v;
}
```

### 4.3 Success & Paginated Responses
- `csilk_respond_ok()` wraps the data into a top-level JSON object and dispatches to `csilk_json(c, CSILK_STATUS_OK, root)`.
- `csilk_respond_page_ex()` constructs the inner `data` object containing `array_key`, `total`, `page`, and `page_size`, then delegates directly to `csilk_respond_ok()`.

---

## 5. Minefolio Integration Plan (`backend/src/common/response.h`)

Minefolio's `response.h` will be refactored into clean forwarding wrappers that preserve existing signatures:

```c
#pragma once

/**
 * @file response.h
 * @brief HTTP JSON response and pagination helpers (thin wrapper over csilk).
 */

#include "csilk/csilk.h"
#include "csilk/core/response.h"

static inline void
respond_ok(csilk_ctx_t* c, csilk_json_t* data)
{
    csilk_respond_ok(c, data);
}

static inline void
respond_ok_null(csilk_ctx_t* c)
{
    csilk_respond_ok_null(c);
}

static inline void
respond_error(csilk_ctx_t* c, int code, const char* msg)
{
    /* Minefolio legacy convention: HTTP 200 with business error code */
    csilk_respond_fail(c, CSILK_STATUS_OK, code, msg);
}

static inline void
respond_unauthorized(csilk_ctx_t* c)
{
    respond_error(c, 1001, "未授权");
}

static inline void
respond_bad_request(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1002, msg ? msg : "参数错误");
}

static inline void
respond_not_found(csilk_ctx_t* c)
{
    respond_error(c, 1003, "资源不存在");
}

static inline void
respond_conflict(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 1004, msg ? msg : "资源已存在");
}

static inline void
respond_forbidden(csilk_ctx_t* c, const char* msg)
{
    respond_error(c, 2001, msg ? msg : "操作被禁止");
}

static inline void
parse_page_params(csilk_ctx_t* c, int64_t* page, int64_t* page_size)
{
    csilk_parse_pagination(c, page, page_size, 20, 500);
}

static inline void
respond_page_ok(csilk_ctx_t* c, csilk_json_t* list, int64_t total, int64_t page, int64_t page_size)
{
    /* Keep "list" key for Minefolio Vue 3 frontend compatibility */
    csilk_respond_page_ex(c, "list", list, total, page, page_size);
}
```

---

## 6. Verification & Test Plan

1. **`csilk` Unit Test Suite (`tests/unit/test_response_envelope.c`)**:
   - Verify `csilk_respond_ok` sets status 200, Content-Type `application/json`, and parses back `{"code":0,"message":"ok","data":{...}}`.
   - Verify `csilk_respond_ok_null` produces `"data": null`.
   - Verify `csilk_respond_fail` formats correctly for small messages (stack fast path) and escaped characters (AST path).
   - Verify `csilk_parse_pagination` handles missing queries, negative values, non-numeric strings, and upper bound caps.
   - Verify `csilk_respond_page` (default `"items"`) and `csilk_respond_page_ex` (custom `"list"`).
   - Run `ctest --output-on-failure` (must pass 226/226 tests).

2. **`Minefolio` Integration Suite**:
   - Recompile backend: `cmake --build backend/build --parallel`.
   - Run end-to-end suite: `./tests/test_link.sh` (38 cases, 143 assertions verifying auth, transactions, pagination, PnL).
   - Run frontend tests: `npm --prefix frontend test`.
