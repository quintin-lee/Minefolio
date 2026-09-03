#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file asset_repo.h
 * @brief 资产 (Assets) 数据访问层接口定义
 *
 * 负责资产账户（包括现金、银行卡、证券股票、基金、加密货币、负债等）
 * 的 CRUD 数据持久化、持仓量/成本/净值维护、行情同步查询与分类关联查询等核心逻辑。
 */

/**
 * @brief 分页查询用户的资产列表，支持按分类过滤
 *
 * 关联 `categories` 表联查分类名称与资产类型 (`asset_type`)，
 * 默认按分类名称与资产名称正序排列 (`ORDER BY c.name, a.name`)。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 页码 (从 1 开始)
 * @param page_size 每页数量
 * @param category_id 分类 ID 过滤字符串（为 NULL 或空字符串时不限制分类）
 * @param[out] total 输出参数，返回匹配条件的资产总数量
 * @return csilk_json_t* 包含资产对象数组的 JSON 指针
 */
csilk_json_t* asset_list(csilk_db_pool_t* pool,
                         int64_t          user_id,
                         int64_t          page,
                         int64_t          page_size,
                         const char*      category_id,
                         int64_t*         total);

/**
 * @brief 根据资产 ID 获取单个资产的详细信息
 *
 * 左连接 categories 表获取 category_name 和 asset_type，
 * 并将 last_sync_at 时间戳转换为 TEXT 格式。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（租户隔离验证）
 * @param id 资产 ID
 * @return csilk_json_t* 包含单条资产信息的 JSON 数组（长度为 1），未命中返回 NULL
 */
csilk_json_t* asset_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 插入新的资产账户记录
 *
 * 包含通用资产属性（名称、账号、余额/市值、币种、备注）
 * 以及投资类资产扩展属性（持仓数量、持仓成本、单位净值、行情代码、行情数据源）。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param category_id 所属分类 ID
 * @param name 资产名称
 * @param account_no 银行卡号/账号（可选）
 * @param current_value 资产当前估值/余额
 * @param currency 结算货币代码（如 "CNY", "USD"）
 * @param note 备注信息
 * @param quantity 投资持仓份额/数量（非投资类为 0）
 * @param cost_basis 持仓总成本（非投资类为 0）
 * @param net_value 单位净值/当前单价（非投资类为 0）
 * @param symbol 行情代码（如 "600519.SH", "AAPL"）
 * @param quote_source 行情源标识（如 "sina", "tencent", "binance"）
 * @return int64_t 成功时返回新插入记录的主键 ID，失败返回 0
 */
int64_t asset_insert(csilk_db_pool_t* pool,
                     int64_t          user_id,
                     int64_t          category_id,
                     const char*      name,
                     const char*      account_no,
                     double           current_value,
                     const char*      currency,
                     const char*      note,
                     double           quantity,
                     double           cost_basis,
                     double           net_value,
                     const char*      symbol,
                     const char*      quote_source);

/**
 * @brief 更新资产的基础属性与行情配置
 *
 * 更新名称、卡号、当前余额、币种、备注、标的代码及行情数据源，
 * 并自动刷新 `updated_at=CURRENT_TIMESTAMP`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @param name 资产名称
 * @param account_no 银行卡号/账号
 * @param current_value 当前余额/估值
 * @param currency 货币代码
 * @param note 备注
 * @param symbol 行情标的代码
 * @param quote_source 行情源
 * @return int 成功更新返回 1，失败或未修改返回 0
 */
int asset_update_basic(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          id,
                       const char*      name,
                       const char*      account_no,
                       double           current_value,
                       const char*      currency,
                       const char*      note,
                       const char*      symbol,
                       const char*      quote_source);

/**
 * @brief 更新投资类资产的持仓数量、成本与净值
 *
 * 供交易变动（买入/卖出/分红/调仓）以及持仓重新核算逻辑调用。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @param net_value 最新单位净值
 * @param quantity 最新持仓数量
 * @param cost_basis 最新总持仓成本
 * @return int 更新成功返回 1，失败返回 0
 */
int asset_update_position(csilk_db_pool_t* pool,
                          int64_t          user_id,
                          int64_t          id,
                          double           net_value,
                          double           quantity,
                          double           cost_basis);

/**
 * @brief 行情同步更新：更新资产最新净值并重算当前市值
 *
 * 执行原子 SQL 计算：若持仓数量 `quantity > 0`，则自动联动更新 `current_value = ROUND(quantity * new_net_value, 2)`，
 * 同时记录 `last_sync_at=CURRENT_TIMESTAMP`。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（为 0 时表示全局后台任务同步，不绑定特定用户）
 * @param asset_id 资产 ID
 * @param new_net_value 获取到的最新市场价格/净值
 * @return int 成功影响的记录行数
 */
int asset_update_market_quote(csilk_db_pool_t* pool,
                              int64_t          user_id,
                              int64_t          asset_id,
                              double           new_net_value);

/**
 * @brief 查询所有配置了行情代码 (`symbol != ''`) 的投资资产列表
 *
 * 用于定时后台任务或手动触发的批量行情刷新同步。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（大于 0 时仅同步该用户，小于等于 0 时全量同步）
 * @return csilk_json_t* 包含 id, user_id, symbol, quote_source, net_value, quantity 等字段的 JSON 数组
 */
csilk_json_t* asset_list_for_sync(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 删除指定资产账户
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int asset_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 检查指定资产是否存在且属于该用户
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 资产 ID
 * @return int 存在且属于该用户返回 1，否则返回 0
 */
int asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 查询分类的资产类型标识 (asset_type)
 *
 * 常用资产类型包括: cash, bank, credit_card, loan, stock, fund, bond, crypto 等。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param category_id 分类 ID
 * @return char* 动态分配的 asset_type 字符串副本（调用方须负责 `free()`），未找到返回 NULL
 */
char* asset_get_category_type(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id);

/**
 * @brief 查询指定资产的所有关联历史交易明细
 *
 * 按交易发生日期逆序 (`transaction_date DESC`) 排列返回。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 目标资产 ID
 * @return csilk_json_t* 包含交易明细记录的 JSON 数组
 */
csilk_json_t* asset_transactions(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);

/**
 * @brief 分页查询资产余额变动审计日志
 */
csilk_json_t* asset_balance_logs_list(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      int64_t          page,
                                      int64_t          page_size,
                                      const char*      asset_id,
                                      int64_t*         total);
