/**
 * @file entity.h
 * @brief 账本领域实体定义 (Domain Ledger Entity)
 *
 * 纯 C 结构体，零外部依赖。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 账本聚合根实体
 */
typedef struct {
    int64_t id;                    /**< 账本主键 ID */
    int64_t owner_id;              /**< 所有者用户 ID */
    char    name[128];             /**< 账本名称 */
    char    description[256];      /**< 账本描述 */
    char    currency[8];           /**< 本位币种代码 */
    char    icon[64];              /**< 图标标识 */
    char    color[16];             /**< 主题色值 */
    bool    is_default;            /**< 是否默认账本 */
    char    invite_code[8];        /**< 邀请码 */
    char    invite_expires_at[32]; /**< 邀请码过期时间 */
    char    created_at[32];        /**< 创建时间 */
    char    updated_at[32];        /**< 更新时间 */
} mf_ledger_t;                     /**
 * @brief 账本成员实体
 */
typedef struct {
    int64_t id;                    /**< 成员记录主键 */
    int64_t ledger_id;             /**< 所属账本 ID */
    int64_t user_id;               /**< 成员用户 ID */
    char    username[64];          /**< 用户名 */
    char    role[16];              /**< 角色 (owner, editor, viewer) */
    char    joined_at[32];         /**< 加入时间 */
} mf_ledger_member_t;

/**
 * @brief 账本列表视图实体（含聚合统计）
 */
typedef struct {
    int64_t id;                    /**< 账本主键 ID */
    int64_t owner_id;              /**< 所有者用户 ID */
    char    name[128];             /**< 账本名称 */
    char    description[256];      /**< 账本描述 */
    char    currency[8];           /**< 本位币种代码 */
    char    icon[64];              /**< 图标标识 */
    char    color[16];             /**< 主题色值 */
    bool    is_default;            /**< 是否默认账本 */
    char    invite_code[8];        /**< 邀请码 */
    char    invite_expires_at[32]; /**< 邀请码过期时间 */
    char    created_at[32];        /**< 创建时间 */
    char    updated_at[32];        /**< 更新时间 */
    char    my_role[16];           /**< 当前用户在该账本中的角色 */
    char    owner_username[64];    /**< 所有者用户名 */
    int64_t member_count;          /**< 成员总数 */
    double  total_assets;          /**< 账本内资产总估值 */
} mf_ledger_list_item_t;
