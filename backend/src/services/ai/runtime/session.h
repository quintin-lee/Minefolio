#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t       session_id;
    int64_t       user_id;
    char          title[128];
    char          provider[64];
    char          model[128];
    csilk_json_t* messages;
} ai_session_context_t;

/**
 * @brief 加载或创建会话上下文
 */
ai_session_context_t* ai_session_load_or_create(csilk_db_pool_t* pool,
                                                int64_t          user_id,
                                                int64_t          session_id,
                                                const char*      provider,
                                                const char*      model);

/**
 * @brief 追加一条新消息到会话历史记录
 */
int ai_session_append_message(csilk_db_pool_t* pool,
                              int64_t          session_id,
                              const char*      role,
                              const char*      content,
                              const char*      model);

/**
 * @brief 释放会话上下文内存
 */
void ai_session_context_free(ai_session_context_t* sctx);

#ifdef __cplusplus
}
#endif
