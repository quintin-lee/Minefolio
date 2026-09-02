#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        draft_id[64];
    char        action_name[64];
    char*       summary;
    char*       payload_json;
    bool        requires_confirmation;
    const char* warning_message;
} ai_confirmation_draft_t;

/**
 * @brief 生成需要用户确认的执行草案
 * @param action_name 操作名称
 * @param summary 操作摘要
 * @param payload_json 原始执行参数 JSON
 * @param warning 警告提示信息
 * @return 构造好的确认草案对象指针（调用方通过 ai_confirmation_draft_free 释放）
 */
ai_confirmation_draft_t* ai_confirmation_draft_create(const char* action_name,
                                                      const char* summary,
                                                      const char* payload_json,
                                                      const char* warning);

/**
 * @brief 释放确认草案内存
 */
void ai_confirmation_draft_free(ai_confirmation_draft_t* draft);

/**
 * @brief 将草案序列化为标准 JSON 字符串
 */
char* ai_confirmation_draft_to_json(const ai_confirmation_draft_t* draft);

#ifdef __cplusplus
}
#endif
