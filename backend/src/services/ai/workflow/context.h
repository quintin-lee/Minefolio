#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    csilk_db_pool_t* pool;
    int64_t          user_id;
    int64_t          session_id;
    csilk_json_t*    params;
    csilk_json_t*    shared_state;
} ai_wf_context_t;

/**
 * @brief 创建工作流执行上下文
 */
ai_wf_context_t* ai_wf_context_create(csilk_db_pool_t*    pool,
                                      int64_t             user_id,
                                      int64_t             session_id,
                                      const csilk_json_t* params);

/**
 * @brief 释放工作流执行上下文
 */
void ai_wf_context_free(ai_wf_context_t* ctx);

/**
 * @brief 设置共享状态变量
 */
void ai_wf_context_set(ai_wf_context_t* ctx, const char* key, const char* value);

/**
 * @brief 获取共享状态变量
 */
const char* ai_wf_context_get(const ai_wf_context_t* ctx, const char* key);

#ifdef __cplusplus
}
#endif
