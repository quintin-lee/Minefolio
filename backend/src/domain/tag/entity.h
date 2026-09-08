#pragma once

/**
 * @file entity.h
 * @brief 标签领域实体定义 (Domain Tag Entity)
 *
 * 纯 C 结构体，零外部依赖，仅使用标准库类型。
 */

#include <stdint.h>

/**
 * @brief 标签聚合根实体
 */
typedef struct {
    int64_t id;             /**< 标签主键 ID */
    int64_t user_id;        /**< 所属用户 ID */
    char    name[128];      /**< 标签显示名称 */
    char    color[32];      /**< 标签颜色值 (如 "#666666") */
    char    created_at[32]; /**< 创建时间 ISO 8601 */
    char    updated_at[32]; /**< 更新时间 ISO 8601 */
} mf_tag_t;
