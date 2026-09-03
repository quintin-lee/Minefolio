#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum ai_permission_level_t
 * @brief 操作/工具所需权限等级
 */
typedef enum {
    AI_PERM_READ = 0,     /**< 只读访问（资产列表、收支统计、持仓分析等） */
    AI_PERM_WRITE = 1,    /**< 普通写入（添加记账、更新标签等） */
    AI_PERM_SENSITIVE = 2 /**< 敏感操作（大额转账、清空资产、修改凭证等） */
} ai_permission_level_t;

/**
 * @enum ai_user_role_t
 * @brief 用户自身权限角色
 */
typedef enum {
    AI_USER_ROLE_GUEST = 0,    /**< 未认证/游客（无任何权限） */
    AI_USER_ROLE_VIEWER = 1,   /**< 只读访客（仅允许 READ） */
    AI_USER_ROLE_STANDARD = 2, /**< 普通用户（允许 READ, WRITE） */
    AI_USER_ROLE_ADMIN = 3     /**< 管理员/账本拥有者（允许 READ, WRITE, SENSITIVE） */
} ai_user_role_t;

/**
 * @brief 获取指定工具或操作所需权限级别
 */
ai_permission_level_t ai_permission_get_level(const char* tool_name);

/**
 * @brief 校验指定用户角色是否满足工具所需权限
 */
bool ai_permission_role_allows(ai_user_role_t user_role, ai_permission_level_t required_level);

/**
 * @brief 校验用户上下文是否具备相应执行权限
 * @param user_id 用户 ID
 * @param required_level 所需最低权限级别
 * @return true 允许执行, false 拒绝
 */
bool ai_permission_check(int64_t user_id, ai_permission_level_t required_level);

/**
 * @brief 获取用户角色
 */
ai_user_role_t ai_permission_get_user_role(int64_t user_id);

/**
 * @brief 运行时设置用户角色（用于测试或特权授权）
 */
void ai_permission_set_user_role(int64_t user_id, ai_user_role_t role);

#ifdef __cplusplus
}
#endif
