#pragma once
#include "csilk/csilk.h"
#include "csilk/drivers/ai.h"
#include "common/ai_config.h"
#include "services/ai/runtime/error.h"
#include "services/ai/runtime/limits.h"
#include "services/ai/policy/permission.h"
#include "services/ai/trace/trace.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 1. 用户与权限 */
    int64_t               user_id;
    ai_permission_level_t perm_level;

    /* 2. 会话标识 */
    int64_t session_id;
    char    session_title[128];

    /* 3. 消息队列 (JSON array of message objects) */
    csilk_json_t* messages;

    /* 4. 可用工具 */
    csilk_ai_tool_t* tools;
    size_t           tool_count;

    /* 5. 模型与超参 */
    char   provider_id[64];
    char   model_name[128];
    double temperature;
    int    max_tokens;
    double top_p;

    /* 6. 限制、统计与取消 */
    ai_runtime_limits_t limits;
    ai_runtime_stats_t  stats;
    volatile bool*      cancel_token;

    /* 7. 扩展元数据与追踪 */
    csilk_json_t* metadata;
    ai_trace_t*   trace;
} ai_runtime_context_t;

void ai_runtime_context_init(ai_runtime_context_t* ctx);
void ai_runtime_context_free(ai_runtime_context_t* ctx);

/* 兼容构建历史 messages 接口 */
csilk_json_t* ai_context_build_messages(const ai_config_t*  cfg,
                                        const csilk_json_t* history_messages,
                                        const char*         user_prompt);

#ifdef __cplusplus
}
#endif
