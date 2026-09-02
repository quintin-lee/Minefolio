#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 工具调用分发器：执行策略检查并调度执行具体工具
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param name 工具名称
 * @param arguments JSON 参数字符串
 * @return 堆分配的 JSON 字符串结果（调用方 free 释放）
 */
char*
ai_tool_dispatch(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* arguments);

/**
 * @brief 解析后参数的工具调用分发器
 */
char* ai_tool_dispatch_parsed(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              const char*      name,
                              csilk_json_t*    args);

#ifdef __cplusplus
}
#endif
