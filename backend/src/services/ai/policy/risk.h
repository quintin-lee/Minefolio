#pragma once
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum ai_risk_level_t
 * @brief AI 操作安全风险等级定义
 */
typedef enum {
    AI_RISK_READ_ONLY = 0, /**< 纯只读查询（如查询资产列表、交易明细、分类等） */
    AI_RISK_LOW = 1,       /**< 低风险辅助（如服务器时间、报表格式化、复利试算等） */
    AI_RISK_MEDIUM = 2,    /**< 中风险操作（如创建交易草案、小额日常记账草稿） */
    AI_RISK_HIGH = 3,      /**< 高风险操作（如执行日常记账入库、常规跨账户转账、删除分类） */
    AI_RISK_CRITICAL = 4   /**< 极高风险操作（如大额资金划转、清仓卖出、重置账本数据） */
} ai_risk_level_t;

/**
 * @brief 将风险等级转换为可读字符串
 */
const char* ai_risk_level_to_string(ai_risk_level_t level);

/**
 * @brief 从字符串解析风险等级
 */
ai_risk_level_t ai_risk_level_from_string(const char* str);

/**
 * @brief 动态评估具体工具调用及参数的风险等级
 *
 * @param tool_name 工具名称
 * @param args 解析后的参数对象（可根据金额等动态提升风险级别）
 * @return 评估得出的风险等级
 */
ai_risk_level_t ai_risk_assess(const char* tool_name, const csilk_json_t* args);

#ifdef __cplusplus
}
#endif
