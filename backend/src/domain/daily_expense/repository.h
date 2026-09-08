/**
 * @file repository.h
 * @brief 日常收支领域仓储契约接口 (Domain Daily Expense Repository Contract)
 */

#pragma once

#include "domain/daily_expense/entity.h"
#include "csilk/csilk.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 多条件分页查询日常收支明细
 * @return 0: 成功, -1: 错误
 */
int mf_daily_expense_repo_list(void*          pool,
                               int64_t        user_id,
                               int64_t        page,
                               int64_t        page_size,
                               const char*    expense_type,
                               const char*    category_id,
                               const char*    tag_ids,
                               const char*    start_date,
                               const char*    end_date,
                               csilk_json_t** out_json,
                               int64_t*       out_total);

/**
 * @brief 按月份统计总收入与总支出
 */
csilk_json_t*
mf_daily_expense_repo_monthly_totals(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份及分类维度聚合统计
 */
csilk_json_t*
mf_daily_expense_repo_monthly_by_category(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份及标签维度聚合统计
 */
csilk_json_t*
mf_daily_expense_repo_monthly_by_tag(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份统计每日收支趋势
 */
csilk_json_t* mf_daily_expense_repo_monthly_daily(void* pool, int64_t user_id, const char* pattern);

/**
 * @brief 创建新的日常收支记录
 * @return 新记录 ID，失败返回 0
 */
int64_t mf_daily_expense_repo_insert(void*       pool,
                                     int64_t     user_id,
                                     int64_t     category_id,
                                     int64_t     asset_id,
                                     const char* expense_type,
                                     double      amount,
                                     const char* currency,
                                     const char* date,
                                     const char* note);

/**
 * @brief 获取单笔收支的回滚属性
 */
int mf_daily_expense_repo_get_snapshot(void*                        pool,
                                       int64_t                      user_id,
                                       int64_t                      id,
                                       mf_daily_expense_snapshot_t* out);

/**
 * @brief 更新日常收支记录
 * @return 0: 成功, -1: 失败
 */
int mf_daily_expense_repo_update(void*       pool,
                                 int64_t     user_id,
                                 int64_t     id,
                                 int64_t     category_id,
                                 int64_t     asset_id,
                                 const char* expense_type,
                                 double      amount,
                                 const char* currency,
                                 const char* date,
                                 const char* note);

/**
 * @brief 删除日常收支记录
 * @return 0: 成功, -1: 失败
 */
int mf_daily_expense_repo_delete(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 检查记录是否存在且属于用户
 */
int mf_daily_expense_repo_exists(void* pool, int64_t user_id, int64_t id);

/**
 * @brief 绑定标签关联
 */
int mf_daily_expense_repo_tag_insert(void* pool, int64_t expense_id, int64_t tag_id);

/**
 * @brief 清除指定记录的所有标签关联
 */
int mf_daily_expense_repo_tag_delete_all(void* pool, int64_t expense_id);

/**
 * @brief 查找或创建标签（由名称或 ID）
 * @return 标签 ID，失败返回 0
 */
int64_t mf_daily_expense_repo_get_or_create_tag(
    void* pool, int64_t user_id, int64_t tag_id, const char* tag_name, const char* tag_color);
