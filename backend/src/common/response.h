#pragma once
#include "csilk/csilk.h"
#include <stdlib.h>

static inline void respond_ok(csilk_ctx_t* c, csilk_json_t* data) {
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "code", 0);
    csilk_json_add_string(r, "message", "ok");
    csilk_json_add_object(r, "data", data);
    csilk_json(c, CSILK_STATUS_OK, r);
}

static inline void respond_ok_null(csilk_ctx_t* c) {
    respond_ok(c, csilk_json_null());
}

static inline void respond_error(csilk_ctx_t* c, int code, const char* msg) {
    csilk_json_t* r = csilk_json_object();
    csilk_json_add_number(r, "code", code);
    csilk_json_add_string(r, "message", msg);
    csilk_json(c, CSILK_STATUS_OK, r);
}

static inline void respond_unauthorized(csilk_ctx_t* c) {
    respond_error(c, 1001, "未授权");
}

static inline void respond_bad_request(csilk_ctx_t* c, const char* msg) {
    respond_error(c, 1002, msg ? msg : "参数错误");
}

static inline void respond_not_found(csilk_ctx_t* c) {
    respond_error(c, 1003, "资源不存在");
}

static inline void respond_conflict(csilk_ctx_t* c, const char* msg) {
    respond_error(c, 1004, msg ? msg : "资源已存在");
}

static inline void respond_forbidden(csilk_ctx_t* c, const char* msg) {
    respond_error(c, 2001, msg ? msg : "操作被禁止");
}

/**
 * @brief Parse pagination query params (page / page_size).
 *
 * Defaults: page=1, page_size=20; page_size capped at 500.
 */
static inline void parse_page_params(csilk_ctx_t* c, int64_t* page, int64_t* page_size) {
    const char* page_str = csilk_get_query(c, "page");
    const char* size_str = csilk_get_query(c, "page_size");
    int64_t p = 1, s = 20;
    if (page_str) {
        p = atoll(page_str);
        if (p < 1) p = 1;
    }
    if (size_str) {
        s = atoll(size_str);
        if (s < 1) s = 20;
        if (s > 500) s = 500;
    }
    *page = p;
    *page_size = s;
}

/** @brief Respond with a paginated envelope: {list, total, page, page_size}. */
static inline void respond_page_ok(csilk_ctx_t* c, csilk_json_t* list, int64_t total,
                                   int64_t page, int64_t page_size) {
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_array(data, "list", list);
    csilk_json_add_number(data, "total", (double)total);
    csilk_json_add_number(data, "page", (double)page);
    csilk_json_add_number(data, "page_size", (double)page_size);
    respond_ok(c, data);
}
