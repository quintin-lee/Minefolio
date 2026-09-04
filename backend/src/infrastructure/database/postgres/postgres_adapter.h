#pragma once

#include "infrastructure/database/adapter_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取 PostgreSQL 驱动适配器操作表
 */
const mf_db_adapter_ops_t* mf_postgres_adapter_get_ops(void);

/**
 * @brief 将带有 '?' 占位符的 SQL 翻译为 PostgreSQL 风格的 '$1, $2, ...'
 * @param sql 原始 SQL 字符串
 * @return 翻译后的新分配字符串，调用者须负责 free()
 */
char* mf_postgres_translate_placeholders(const char* sql);

#ifdef __cplusplus
}
#endif
