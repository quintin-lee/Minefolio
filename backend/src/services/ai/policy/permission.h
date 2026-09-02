#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_PERM_READ = 0,     /**< 只读访问（资产列表、收支统计、持仓分析等） */
    AI_PERM_WRITE = 1,    /**< 普通写入（添加记账、更新标签等） */
    AI_PERM_SENSITIVE = 2 /**< 敏感操作（大额转账、清空资产、修改凭证等） */
} ai_permission_level_t;

/**
 * @brief 检查指定工具或操作所需权限级别
 * @param tool_name 工具或操作名称
 * @return 对应的权限级别
 */
ai_permission_level_t ai_permission_get_level(const char* tool_name);

/**
 * @brief 校验用户上下文是否具备相应执行权限
 * @param user_id 用户 ID
 * @param required_level 所需最低权限级别
 * @return true 允许执行, false 拒绝
 */
bool ai_permission_check(int64_t user_id, ai_permission_level_t required_level);

#ifdef __cplusplus
}
#endif
