#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct ai_tool_context_t
 * @brief AI 工具执行上下文环境
 *
 * 封装工具执行所需的环境变量，避免工具直接依赖 HTTP 请求。
 */
typedef struct {
    csilk_db_pool_t* pool;         /**< 数据库连接池指针 */
    int64_t          user_id;      /**< 当前操作用户 ID */
    int64_t          session_id;   /**< 当前 AI 对话会话 ID */
    char             trace_id[64]; /**< 当前请求链路追踪 ID */
    uint32_t         permissions;  /**< 用户权限位图 */
    char             locale[16];   /**< 用户语言区域 (如 "zh-CN") */
    char             timezone[32]; /**< 用户时区 (如 "Asia/Shanghai") */
    csilk_json_t*    metadata;     /**< 扩展元数据键值对 */
} ai_tool_context_t;

/**
 * @brief 创建工具执行上下文
 */
ai_tool_context_t* ai_tool_context_create(csilk_db_pool_t* pool,
                                          int64_t          user_id,
                                          int64_t          session_id,
                                          const char*      trace_id);

/**
 * @brief 释放工具执行上下文
 */
void ai_tool_context_free(ai_tool_context_t* ctx);

#ifdef __cplusplus
}
#endif
