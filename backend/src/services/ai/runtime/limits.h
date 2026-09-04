#pragma once
#include "services/ai/runtime/error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int     max_iterations; /**< 最大循环轮数（默认 10） */
    int64_t timeout_ms;     /**< 最大执行耗时毫秒数（0 表示不限） */
    int     token_budget;   /**< 最大累计 Token 预算 (0 表示不限) */
    int     tool_budget;    /**< 最大累计工具执行次数 (0 表示不限) */
    double  cost_budget;    /**< 最大累计费用预算 (0 表示不限) */
} ai_runtime_limits_t;

typedef struct {
    int     iterations_done;   /**< 已完成轮数 */
    int     prompt_tokens;     /**< 累计输入 Token */
    int     completion_tokens; /**< 累计输出 Token */
    int     total_tokens;      /**< 累计总 Token */
    int     tool_calls_count;  /**< 累计工具调用次数 */
    double  total_cost;        /**< 累计估计成本 (USD) */
    int64_t elapsed_ms;        /**< 累计已消耗时间 (ms) */
} ai_runtime_stats_t;

ai_runtime_limits_t ai_runtime_limits_default(void);

bool ai_runtime_limits_check_pre_turn(const ai_runtime_limits_t* limits,
                                      const ai_runtime_stats_t*  stats,
                                      const volatile bool*       cancel_token,
                                      int64_t                    current_elapsed_ms,
                                      ai_runtime_status_t*       out_status);

bool ai_runtime_limits_check_tool_budget(const ai_runtime_limits_t* limits,
                                         const ai_runtime_stats_t*  stats,
                                         int                        requested_tools,
                                         ai_runtime_status_t*       out_status);

void ai_runtime_limits_record_tokens(ai_runtime_stats_t* stats,
                                     int                 prompt_tokens,
                                     int                 completion_tokens,
                                     double              cost);

void ai_runtime_limits_record_tool_call(ai_runtime_stats_t* stats, int count);

#ifdef __cplusplus
}
#endif
