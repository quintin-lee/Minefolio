#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>

/**
 * @file cashflow_repo.h
 * @brief 周期性被动现金流计划 (Cashflow Schedules) 数据访问层接口
 *
 * 负责被动现金流计划（如股票分红、债券派息、银行理财利息、房租收入等）
 * 的 CRUD 持久化、有效状态计划检索以及月度实际收入交易对比查询。
 */

/**
 * @brief 查询用户的所有现金流计划列表
 *
 * 联查来源资产 (source_asset_id) 和目标入账资金账户 (target_asset_id)，
 * 按 ID 倒序排列。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含现金流计划列表的 JSON 数组
 */
csilk_json_t* cashflow_schedule_list(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据计划 ID 查询单条现金流计划详情
 *
 * 包含计划基本配置与来源/目标资产的名称、代码和币种。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（租户隔离验证）
 * @param id 计划 ID
 * @return csilk_json_t* 包含单个计划对象的 JSON 数组（长度为 1）；若未命中返回空数组或 NULL
 */
csilk_json_t* cashflow_schedule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 创建新的周期性现金流计划记录
 *
 * 默认初始化状态为 `status='active'`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 关联用户 ID
 * @param source_asset_id 产生现金流的来源资产（如标的股票/房产/理财）
 * @param target_asset_id 资金实际流入的目标账户（如银行卡/活期钱包）
 * @param name 计划名称描述
 * @param flow_type 现金流类型（如 "dividend", "interest", "rent", "salary" 等）
 * @param frequency 发生周期频率（如 "monthly", "quarterly", "semi-annual", "annual", "once"）
 * @param start_date 计划起效日期 (格式: YYYY-MM-DD)
 * @param end_date 计划截止日期（可选，空字符串表示永续）
 * @param expected_amount 预期每期流入金额
 * @param note 备注说明
 * @return int64_t 成功返回新记录的主键 ID，失败返回 -1
 */
int64_t cashflow_schedule_create(csilk_db_pool_t* pool,
                                 int64_t          user_id,
                                 int64_t          source_asset_id,
                                 int64_t          target_asset_id,
                                 const char*      name,
                                 const char*      flow_type,
                                 const char*      frequency,
                                 const char*      start_date,
                                 const char*      end_date,
                                 double           expected_amount,
                                 const char*      note);

/**
 * @brief 更新指定的现金流计划配置
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @param source_asset_id 来源资产 ID
 * @param target_asset_id 目标资产 ID
 * @param name 计划名称
 * @param flow_type 现金流类型
 * @param frequency 周期频率
 * @param start_date 起始日期
 * @param end_date 终止日期
 * @param expected_amount 预期每期金额
 * @param note 备注说明
 * @return int 成功返回 0，失败返回 -1
 */
int cashflow_schedule_update(csilk_db_pool_t* pool,
                             int64_t          user_id,
                             int64_t          id,
                             int64_t          source_asset_id,
                             int64_t          target_asset_id,
                             const char*      name,
                             const char*      flow_type,
                             const char*      frequency,
                             const char*      start_date,
                             const char*      end_date,
                             double           expected_amount,
                             const char*      note);

/**
 * @brief 删除指定的现金流计划记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 计划 ID
 * @return int 成功返回 0，失败返回 -1
 */
int cashflow_schedule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 查询用户当前所有处于有效运行状态 (`status='active'`) 的现金流计划
 *
 * 供现金流日历预测、被动收入月度投影以及现金流图表渲染计算调用。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含所有活跃状态计划的 JSON 数组
 */
csilk_json_t* cashflow_list_active_schedules(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 查询用户在特定月份内实际发生的收入类交易流水
 *
 * 筛选 `transaction_type IN ('income', 'deposit')` 且 `transaction_date LIKE 'YYYY-MM%'`，
 * 用于与现金流计划预期金额进行对比对齐分析。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param year_month 年月前缀字符串 (例如 "2026-09")
 * @return csilk_json_t* 包含实际收入交易记录的 JSON 数组（按日期正序排列）
 */
csilk_json_t*
cashflow_query_actual_transactions(csilk_db_pool_t* pool, int64_t user_id, const char* year_month);
