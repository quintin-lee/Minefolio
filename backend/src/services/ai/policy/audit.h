#pragma once
#include "services/ai/policy/risk.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_AUDIT_STAGE_PROPOSAL = 0,     /**< 拟录入与草案生成阶段 */
    AI_AUDIT_STAGE_CONFIRMATION = 1, /**< 用户显式确认阶段 */
    AI_AUDIT_STAGE_EXECUTION = 2,    /**< 事务执行阶段 */
    AI_AUDIT_STAGE_REJECTION = 3     /**< 策略拦截或异常驳回阶段 */
} ai_audit_stage_t;

typedef struct {
    ai_audit_stage_t stage;
    int64_t          actor_id;
    int64_t          session_id;
    char             tool[64];
    char             arguments_hash[65];
    ai_risk_level_t  risk;
    int64_t          timestamp;
    bool             success;
    char             error_message[128];
    char             result_summary[256];
} ai_audit_record_t;

/**
 * @brief 记录结构化审计日志（自动过滤敏感密钥/令牌，仅记录参数摘要与指纹）
 */
void ai_audit_log(const ai_audit_record_t* record);

/**
 * @brief 查询内存审计日志列表（用于审计巡检与自动化测试核验）
 */
const ai_audit_record_t* ai_audit_get_recent(size_t* count);

/**
 * @brief 清理内存审计日志缓存（主要供单元测试使用）
 */
void ai_audit_clear(void);

#ifdef __cplusplus
}
#endif
