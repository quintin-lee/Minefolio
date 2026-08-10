#pragma once
#include "csilk/csilk.h"

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
