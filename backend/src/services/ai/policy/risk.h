#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_RISK_LOW = 0,     /**< 低风险操作（如普通查询、小额日常记账） */
    AI_RISK_MEDIUM = 1,  /**< 中风险操作（如常规转账、预算修改） */
    AI_RISK_HIGH = 2,    /**< 高风险操作（如大额资金划转、调仓、删除分类） */
    AI_RISK_CRITICAL = 3 /**< 极高风险操作（如清仓卖出、销毁账本、重置数据） */
} ai_risk_level_t;

/**
 * @brief 评估具体工具调用参数的风险等级
 * @param tool_name 工具名称
 * @param args 解析后的参数对象
 * @return 风险等级
 */
ai_risk_level_t ai_risk_assess(const char* tool_name, const csilk_json_t* args);

#ifdef __cplusplus
}
#endif
