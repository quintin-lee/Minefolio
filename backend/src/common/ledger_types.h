#pragma once

/**
 * @file ledger_types.h
 * @brief 多账本空间与成员权限数据结构定义
 *
 * 定义多账本空间实体（ledger_t）以及账本成员及其角色权限模型（ledger_member_t）。
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * @struct ledger_t
 * @brief 账本空间实体模型
 */
typedef struct {
    int64_t id;                    /**< 账本唯一主键 ID */
    int64_t owner_id;              /**< 账本所有者（创建者）用户 ID */
    char    name[128];             /**< 账本名称 */
    char    description[256];      /**< 账本详细描述 */
    char    currency[16];          /**< 账本结算基准货币代码（如 "CNY", "USD"） */
    char    icon[64];              /**< 账本展示图标标识符 */
    char    color[32];             /**< 账本主题配色（十六进制颜色值） */
    bool    is_default;            /**< 是否为用户的默认账本空间 */
    char    invite_code[32];       /**< 账本加入邀请码 */
    char    invite_expires_at[32]; /**< 邀请码过期时间字符串 ("YYYY-MM-DD HH:MM:SS") */
    char    created_at[32];        /**< 记录创建时间戳 */
    char    updated_at[32];        /**< 记录最后更新时间戳 */
} ledger_t;

/**
 * @struct ledger_member_t
 * @brief 账本成员及角色关系模型
 */
typedef struct {
    int64_t id;            /**< 成员关联记录主键 ID */
    int64_t ledger_id;     /**< 关联账本 ID */
    int64_t user_id;       /**< 关联用户 ID */
    char    username[128]; /**< 用户名快照 */
    char
        role[32]; /**< 成员在账本中的权限角色（'owner' 所有者, 'editor' 编辑者, 'viewer' 只读者） */
    char joined_at[32]; /**< 加入账本时间戳 */
} ledger_member_t;
