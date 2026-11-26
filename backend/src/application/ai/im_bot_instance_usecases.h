#pragma once

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @file iLink 实例用例：App owner 操作与网关 service-token 操作编排。 */

int           im_instance_usecase_create(void*          pool,
                                         int64_t        user_id,
                                         const char*    platform,
                                         const char*    adapter,
                                         csilk_json_t** out,
                                         int*           code,
                                         char*          err,
                                         size_t         err_cap);
csilk_json_t* im_instance_usecase_list(void* pool, int64_t user_id);
csilk_json_t* im_instance_usecase_get(void* pool, int64_t user_id, int64_t id);
int           im_instance_usecase_relogin(
    void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap);
int im_instance_usecase_verify_code(void*       pool,
                                    int64_t     user_id,
                                    int64_t     id,
                                    const char* code,
                                    int*        code_out,
                                    char*       err,
                                    size_t      err_cap);
int im_instance_usecase_set_enabled(
    void* pool, int64_t user_id, int64_t id, int enabled, int* code, char* err, size_t err_cap);
int im_instance_usecase_set_capability(void*       pool,
                                       int64_t     user_id,
                                       int64_t     id,
                                       const char* capability,
                                       int*        code,
                                       char*       err,
                                       size_t      err_cap);
int im_instance_usecase_delete(
    void* pool, int64_t user_id, int64_t id, int* code, char* err, size_t err_cap);

csilk_json_t* im_instance_gateway_list(void* pool);
csilk_json_t* im_instance_gateway_get(void* pool, int64_t id);
int           im_instance_gateway_status(
    void* pool, int64_t id, csilk_json_t* body, int* code, char* err, size_t err_cap);
int im_instance_gateway_heartbeat_ext(void*          pool,
                                      int64_t        id,
                                      const char*    last_inbound_at,
                                      csilk_json_t** out,
                                      int*           code,
                                      char*          err,
                                      size_t         err_cap);
int im_instance_gateway_heartbeat(
    void* pool, int64_t id, csilk_json_t** out, int* code, char* err, size_t err_cap);
int           im_instance_gateway_chat(void*          pool,
                                       int64_t        id,
                                       const char*    im_user_id,
                                       const char*    message,
                                       csilk_json_t** out_sync,
                                       int64_t*       task_id,
                                       bool*          dedup,
                                       int64_t*       session_id,
                                       int*           code,
                                       char*          err,
                                       size_t         err_cap);
int           im_instance_gateway_confirm(void*       pool,
                                          int64_t     id,
                                          const char* im_user_id,
                                          const char* confirm_code,
                                          char**      out_reply,
                                          int*        code,
                                          char*       err,
                                          size_t      err_cap);
csilk_json_t* im_instance_gateway_task(void* pool, int64_t id, int64_t task_id, int* code);
