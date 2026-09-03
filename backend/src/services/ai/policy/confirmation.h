#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

/**
 * @brief 基于 HMAC-SHA256 生成防伪防篡改的草案确认 Token (5分钟有效期)
 */
char* ai_confirmation_create_token(int64_t user_id, double amount, const char* a, const char* b);

/**
 * @brief 校验草案确认 Token 有效性与完整性
 * @return 1 为合法有效，0 为缺失/篡改/过期
 */
int ai_confirmation_verify_token(
    int64_t user_id, double amount, const char* a, const char* b, const char* token);

#ifdef __cplusplus
}
#endif
