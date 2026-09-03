#pragma once

#include <stddef.h>
#include "domain/asset/entity.h"

/**
 * @brief 校验资产实体的领域业务规则
 */
int mf_asset_rule_validate(const mf_asset_t* asset, char* err_buf, size_t err_len);

/**
 * @brief 投资类资产自动推导市值与初始成本
 * @note 若为股票/基金/债券/加密货币且 quantity > 0, net_value > 0，则自动将 current_value 设为 quantity * net_value；
 *       若 cost_basis <= 0，则同步默认 cost_basis = current_value
 */
int mf_asset_rule_derive_investment_values(mf_asset_t* asset);

/**
 * @brief 计算持仓浮动盈亏与盈亏比例
 */
int
mf_asset_rule_calculate_floating_pnl(const mf_asset_t* asset, money_t* out_pnl, double* out_pct);
