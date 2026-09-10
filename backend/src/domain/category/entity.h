#pragma once

/**
 * @file entity.h
 * @brief 分类领域实体定义 (Domain Category Entity)
 *
 * 纯 C 结构体，零外部依赖。
 */

#include <stdint.h>

/**
 * @brief 分类聚合根实体
 */
typedef struct {
    int64_t id;             /**< 分类主键 ID */
    int64_t user_id;        /**< 所属用户 ID */
    int64_t ledger_id;      /**< 所属账本 ID；0 表示尚未回填的历史记录 */
    char    name[128];      /**< 分类显示名称 */
    int64_t parent_id;      /**< 父分类 ID（0 表示顶级） */
    char    type[32];       /**< 分类主类别 ("expense","income","asset","transaction") */
    char    asset_type[32]; /**< 资产细分子类型 ("cash","stock","fund" 等) */
    char    currency[8];    /**< 预设货币代码 */
    char    icon[64];       /**< 图标名称 */
    int     sort_order;     /**< 排序权重 */
    char    created_at[32]; /**< 创建时间 */
} mf_category_t;
