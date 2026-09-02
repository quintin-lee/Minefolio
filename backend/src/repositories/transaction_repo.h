#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file transaction_repo.h
 * @brief 核心交易流水 (Transactions) 与手续费级联管理数据访问层接口
 *
 * 负责系统内所有资产变动交易流水（买入/卖出/分红/转账/手续费等）的 CRUD 数据持久化。
 * 包含多条件分页筛选、月度流水汇总、持仓回滚前快照提取、以及基于 parent_tx_id 的手续费子行级联回滚与删除。
 */

/**
 * @brief 多条件分页查询交易流水记录列表
 *
 * 动态拼接 WHERE 筛选子句（资产、分类、交易类型、源类型、日期范围），
 * 左连接主资产 (assets)、关联资金账户 (linked assets) 以及分类 (categories) 表，
 * 默认按交易发生日期倒序 (`ORDER BY t.transaction_date DESC`) 分页返回。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param page 当前页码 (从 1 开始)
 * @param page_size 每页数量
 * @param asset_id 可选主资产 ID 过滤
 * @param category_id 可选分类 ID 过滤
 * @param type 可选交易类型编码 (如 "buy", "sell", "dividend", "fee" 等)
 * @param source_type 可选来源业务类型 (如 "manual", "import", "dca" 等)
 * @param start_date 可选起始日期 (YYYY-MM-DD)
 * @param end_date 可选截止日期 (YYYY-MM-DD)
 * @param[out] total 输出参数，返回符合条件的总交易笔数指针
 * @return csilk_json_t* 包含交易明细及关联资产名称的 JSON 数组
 */
csilk_json_t* tx_list(csilk_db_pool_t* pool,
                      int64_t          user_id,
                      int64_t          page,
                      int64_t          page_size,
                      const char*      asset_id,
                      const char*      category_id,
                      const char*      type,
                      const char*      source_type,
                      const char*      start_date,
                      const char*      end_date,
                      int64_t*         total);

/**
 * @brief 按月份通配模式统计交易总流水、资金流入流出总额与笔数
 *
 * 计算指标：
 * - `total_volume`: 交易总发生额
 * - `inflows`: 流入资金总和 (`direction='in'`)
 * - `outflows`: 流出资金总和 (`direction='out'`)
 * - `count`: 交易总笔数
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param pattern 年月通配字符串 (例如 "2026-09%")
 * @return csilk_json_t* 包含统计结果的 JSON 数组
 */
csilk_json_t* tx_monthly(csilk_db_pool_t* pool, int64_t user_id, const char* pattern);

/**
 * @brief 插入一条新的交易流水记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 主目标资产 ID
 * @param linked_asset_id 关联资金资产 ID（无关联账户传入 0）
 * @param category_id 业务分类 ID
 * @param source_type 交易来源类型（如 "manual", "dca", "import"）
 * @param transaction_type 交易类型代码 (如 "buy", "sell", "dividend", "income", "expense" 等)
 * @param direction 主资产资金变动方向 ("in" 或 "out")
 * @param linked_direction 关联账户资金变动方向 ("out" 或 NULL)
 * @param amount 交易发生金额
 * @param price_per_unit 成交单价/单位净值
 * @param quantity 成交数量/份额
 * @param fee 交易手续费金额
 * @param currency 结算货币代码（如 "CNY"）
 * @param date 交易发生日期 (YYYY-MM-DD)
 * @param note 备注文本
 * @return int64_t 成功时返回新交易记录的主键 ID，失败返回 0
 */
int64_t tx_insert(csilk_db_pool_t* pool,
                  int64_t          user_id,
                  int64_t          asset_id,
                  int64_t          linked_asset_id,
                  int64_t          category_id,
                  const char*      source_type,
                  const char*      transaction_type,
                  const char*      direction,
                  const char*      linked_direction,
                  double           amount,
                  double           price_per_unit,
                  double           quantity,
                  double           fee,
                  const char*      currency,
                  const char*      date,
                  const char*      note);

/**
 * @brief 更新指定的交易流水记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 待修改交易 ID
 * @param transaction_type 新交易类型
 * @param direction 主资产变动方向
 * @param linked_direction 关联资产变动方向
 * @param amount 交易金额
 * @param price_per_unit 单价
 * @param quantity 份额
 * @param currency 货币
 * @param date 交易日期
 * @param note 备注
 * @param category_id 分类 ID
 * @param source_type 来源类型
 * @param linked_asset_id 关联资金账户 ID (0 将转为 NULL)
 * @return int 成功返回 1，失败返回 0
 */
int tx_update(csilk_db_pool_t* pool,
              int64_t          user_id,
              int64_t          id,
              const char*      transaction_type,
              const char*      direction,
              const char*      linked_direction,
              double           amount,
              double           price_per_unit,
              double           quantity,
              const char*      currency,
              const char*      date,
              const char*      note,
              int64_t          category_id,
              const char*      source_type,
              int64_t          linked_asset_id);

/**
 * @brief 获取交易修改/删除前的原始快照字段
 *
 * 提取 `asset_id, linked_asset_id, amount, transaction_type, quantity, price_per_unit, fee`，
 * 供业务层计算反向 Delta 回滚旧持仓和旧账户余额。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @return csilk_json_t* 包含旧交易参数的 JSON 数组
 */
csilk_json_t* tx_get_old(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 删除指定的交易流水记录
 *
 * 注意：在删除带有手续费的父交易时，调用方必须先通过 `tx_child_fee_rows` 回滚手续费资金变动，
 * 并调用 `tx_delete_fee_children` 清理子行，最后调用本函数。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 交易 ID
 * @return int 成功返回 1，失败返回 0
 */
int tx_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 查询挂载在指定父交易下的所有手续费子行记录 (Fee Child Rows)
 *
 * 查询条件：`parent_tx_id=? AND user_id=? AND transaction_type='fee'`。
 * 用于在删除或编辑父交易前准确找出所有子手续费扣款并逆向冲销余额。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易的主键 ID
 * @return csilk_json_t* 包含 id, linked_asset_id, amount, note 的 JSON 数组
 */
csilk_json_t* tx_child_fee_rows(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);

/**
 * @brief 级联删除指定父交易下的所有手续费子行记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_tx_id 父交易的主键 ID
 * @return int 成功删除返回 1，失败返回 0
 */
int tx_delete_fee_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_tx_id);

/**
 * @brief 校验资产是否存在且属于该用户
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param asset_id 待校验资产 ID
 * @return int 存在返回 1，不存在返回 0
 */
int tx_asset_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id);
