#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file daily_expense_repo.h
 * @brief 日常收支记账 (Daily Expenses) 数据访问层接口定义
 *
 * 负责日常收入与支出明细 (daily_expenses) 的多条件复杂分页筛选、聚合报表统计
 * （月度收支总览、按分类聚合、按标签聚合、每日收支趋势），以及标签多对多关联 (`expense_tags`) 的维护。
 */

/**
 * @brief 多条件分页查询日常收支明细列表
 *
 * 支持收支类型 (expense_type)、分类 ID (category_id)、多标签 (tag_ids 逗号分隔)、
 * 日期区间 (start_date ~ end_date) 组合筛选。
 * 每一项收支记录内嵌标签 JSON 数组：`tags: [{id, name, color}, ...]`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页数量
 * @param expense_type 支出/收入类型过滤 ("expense", "income")，可选
 * @param category_id 分类 ID 过滤，可选
 * @param tag_ids 标签 ID 列表字符串（如 "1,2,3"），可选
 * @param start_date 起始日期 (YYYY-MM-DD)，可选
 * @param end_date 截止日期 (YYYY-MM-DD)，可选
 * @param[out] total 输出参数，返回符合条件的总记录数
 * @return csilk_json_t* 包含明细列表与嵌套标签的 JSON 数组
 */
csilk_json_t* de_list(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          page,
                      int64_t          page_size,
                      const char*      expense_type,
                      const char*      category_id,
                      const char*      tag_ids,
                      const char*      start_date,
                      const char*      end_date,
                      int64_t*         total);

/**
 * @brief 按月份模糊匹配统计总收入与总支出
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（保留参数）
 * @param pattern 日期通配模式（如 "2026-09%"）
 * @return csilk_json_t* 包含 `total_income` 与 `total_expense` 的 JSON 数组
 */
csilk_json_t* de_monthly_totals(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份及分类维度聚合统计收支汇总
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（保留参数）
 * @param pattern 日期通配模式（如 "2026-09%"）
 * @return csilk_json_t* 包含 `category_name`, `expense_type`, `amount` 的 JSON 数组（按金额降序排列）
 */
csilk_json_t* de_monthly_by_category(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份及标签维度聚合统计收支汇总
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（保留参数）
 * @param pattern 日期通配模式（如 "2026-09%"）
 * @return csilk_json_t* 包含 `tag_name`, `amount`, `count` 的 JSON 数组（按金额降序排列）
 */
csilk_json_t* de_monthly_by_tag(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);

/**
 * @brief 按月份统计每日收支趋势数据
 *
 * 按日聚合每日的 income 与 expense。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（保留参数）
 * @param pattern 日期通配模式（如 "2026-09%"）
 * @return csilk_json_t* 包含 `expense_date`, `income`, `expense` 的 JSON 数组（按日期正序排列）
 */
csilk_json_t* de_monthly_daily(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);

/**
 * @brief 创建新的日常收支记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param category_id 关联分类 ID
 * @param asset_id 扣款/收款资产账户 ID
 * @param expense_type 收支类型 ("expense" 或 "income")
 * @param amount 金额
 * @param currency 货币代码（如 "CNY"）
 * @param date 记账日期 (YYYY-MM-DD)
 * @param note 备注文本
 * @return int64_t 成功时返回新记录主键 ID，失败返回 0
 */
int64_t de_insert(csilk_db_pool_t* pool,
                  int64_t          user_id,
                  int64_t          category_id,
                  int64_t          asset_id,
                  const char*      expense_type,
                  double           amount,
                  const char*      currency,
                  const char*      date,
                  const char*      note);

/**
 * @brief 查询单笔收支的关键回滚属性（金额、收支类型、扣款资产 ID）
 *
 * 供业务层在更新或删除前反向调整资产账户余额。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 记录 ID
 * @return csilk_json_t* 包含 amount, expense_type, asset_id 的 JSON 数组
 */
csilk_json_t* de_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 更新日常收支记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 记录 ID
 * @param category_id 新分类 ID
 * @param asset_id 新资产账户 ID
 * @param expense_type 新收支类型
 * @param amount 新金额
 * @param currency 货币代码
 * @param date 记账日期
 * @param note 备注
 * @return int 成功返回 1，失败返回 0
 */
int de_update(csilk_db_pool_t* pool,
              int64_t          user_id,
              int64_t          id,
              int64_t          category_id,
              int64_t          asset_id,
              const char*      expense_type,
              double           amount,
              const char*      currency,
              const char*      date,
              const char*      note);

/**
 * @brief 删除日常收支记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 记录 ID
 * @return int 成功删除返回 1，失败返回 0
 */
int de_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 为日常收支绑定标签关联记录
 *
 * 插入 `expense_tags` 表，若已存在则忽略 (`INSERT OR IGNORE`)。
 *
 * @param pool 数据库连接池指针
 * @param expense_id 收支记录 ID
 * @param tag_id 标签 ID
 * @return int 成功返回 1，失败返回 0
 */
int de_tag_insert(csilk_db_pool_t* pool, int64_t expense_id, int64_t tag_id);

/**
 * @brief 清除指定收支记录关联的所有标签
 *
 * 供重新绑定或删除收支记录时清理中间表。
 *
 * @param pool 数据库连接池指针
 * @param expense_id 收支记录 ID
 * @return int 成功返回 1，失败返回 0
 */
int de_tag_delete_all(csilk_db_pool_t* pool, int64_t expense_id);
