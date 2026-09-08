#pragma once

#include "domain/market/repository.h"
#include "csilk/csilk.h"

/* 基础设施层：基于 SQL/SQLite 实现市场行情及汇率仓储契约 */

/**
 * @brief 查询指定资产的历史价格走势记录列表
 */
csilk_json_t*
mf_market_repo_price_history_list(void* pool, int64_t user_id, int64_t asset_id, int limit);
