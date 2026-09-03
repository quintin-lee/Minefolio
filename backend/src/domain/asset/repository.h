#pragma once

#include <stdint.h>
#include <stddef.h>
#include "domain/asset/entity.h"

/**
 * @brief 资产仓储抽象契约接口 (Domain Asset Repository Contract)
 * @note 纯 C 契约，入参出参仅传递领域实体与标量，严禁返回 JSON 节点或编写 SQL
 */

/**
 * @brief 根据资产 ID 查询单条资产
 * @return 0: 成功查到, 1: 不存在, -1: 数据库错误
 */
int mf_asset_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_asset_t* out_asset);

/**
 * @brief 持久化保存新资产
 * @return 0: 成功, -1: 失败
 */
int mf_asset_repo_save(void* db_pool, const mf_asset_t* asset, int64_t* out_id);

/**
 * @brief 更新资产基础属性 (名称、卡号、币种、备注、行情代码与源)
 * @return 0: 成功, -1: 失败
 */
int mf_asset_repo_update_basic(void* db_pool, const mf_asset_t* asset);

/**
 * @brief 更新投资类资产的持仓数量、总成本与单位净值
 * @return 0: 成功, -1: 失败
 */
int mf_asset_repo_update_position(void* db_pool, int64_t user_id, int64_t id,
                                  price_t net_value, quantity_t quantity, money_t cost_basis);

/**
 * @brief 删除单条资产
 * @return 0: 成功, -1: 失败
 */
int mf_asset_repo_delete(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 校验资产是否存在且属于该用户
 * @return 1: 存在, 0: 不存在
 */
int mf_asset_repo_exists(void* db_pool, int64_t user_id, int64_t id);
