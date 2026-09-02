#pragma once
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_SPAN_LLM = 0,     /**< LLM 推理调用跨度 */
    AI_SPAN_TOOL = 1,    /**< Function Calling 工具执行跨度 */
    AI_SPAN_WORKFLOW = 2 /**< 工作流步骤执行跨度 */
} ai_span_type_t;

typedef struct {
    char            name[64];
    ai_span_type_t  type;
    struct timespec start_time;
    struct timespec end_time;
    long            duration_ms;
    int             tokens;
    char            status[16];
    char*           error;
} ai_span_t;

/**
 * @brief 启动一个新的执行跨度
 */
void ai_span_start(ai_span_t* span, const char* name, ai_span_type_t type);

/**
 * @brief 结束执行跨度并计算持续耗时
 */
void ai_span_finish(ai_span_t* span, const char* status, const char* error);

#ifdef __cplusplus
}
#endif
