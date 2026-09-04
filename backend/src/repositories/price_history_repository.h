#pragma once

/**
 * @file price_history_repository.h
 * @brief 历史价格时间序列数据访问层（纯数据映射，消除任何数据库引擎分支）
 */

#include "infrastructure/database/database.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int price_history_repo_record(mf_db_t*    db,
                              int64_t     asset_id,
                              const char* price_date,
                              double      price,
                              const char* currency);

#ifdef __cplusplus
}
#endif
