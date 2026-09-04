#pragma once

#include "infrastructure/database/adapter_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3 sqlite3;

/**
 * @brief 获取 SQLite 驱动适配器操作表
 */
const mf_db_adapter_ops_t* mf_sqlite_adapter_get_ops(void);

/**
 * @brief 从现有原生 sqlite3 连接句柄包装为适配器句柄
 */
int mf_sqlite_adapter_wrap_native(sqlite3* db, void** out_handle);

#ifdef __cplusplus
}
#endif
