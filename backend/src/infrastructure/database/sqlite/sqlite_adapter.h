#pragma once

#include "infrastructure/database/adapter_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取 SQLite 驱动适配器操作表
 */
const mf_db_adapter_ops_t* mf_sqlite_adapter_get_ops(void);

#ifdef __cplusplus
}
#endif
