/**
 * @file ai_settings_repo.c
 * @brief 全局 AI 设置配置持久化数据访问层具体实现
 *
 * 实现了基于单例主键 (id=1) 的全局 AI 配置加载与原子 UPSERT 保存逻辑。
 */

#include "repositories/ai_settings_repo.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 加载全局 AI 服务的 JSON 配置内容
 *
 * 执行 SQL：`SELECT config_json FROM ai_settings WHERE id=1`。
 * 从查询返回的 JSON 数组第 0 个元素中读取 `config_json` 字段并通过 `strdup` 复制返回。
 *
 * @param pool 数据库连接池指针
 * @return char* 成功返回配置字符串副本，失败或记录不存在返回 NULL
 */
char*
ai_settings_load(csilk_db_pool_t* pool)
{
    csilk_json_t* r = csilk_db_query_param_json(
        pool, "SELECT config_json FROM ai_settings WHERE id=1", (const char*[]){NULL});
    if (!r || csilk_json_array_size(r) == 0) {
        csilk_json_free(r);
        return NULL;
    }
    const char* json = csilk_json_get_string(csilk_json_array_get(r, 0), "config_json");
    char*       result = json ? strdup(json) : NULL;
    csilk_json_free(r);
    return result;
}

/**
 * @brief 持久化保存全局 AI 配置 JSON 字符串
 *
 * 执行参数化 UPSERT SQL：
 * `INSERT INTO ai_settings (id, config_json, updated_at) VALUES (1, ?, CURRENT_TIMESTAMP) ON CONFLICT(id) DO UPDATE SET config_json=excluded.config_json, updated_at=CURRENT_TIMESTAMP`
 * 支持 SQLite 与 PostgreSQL 的标准 UPSERT 语法。
 *
 * @param pool 数据库连接池指针
 * @param config_json 待保存的配置 JSON 字符串
 * @return int 成功返回 0，数据库操作失败返回 -1
 */
int
ai_settings_save(csilk_db_pool_t* pool, const char* config_json)
{
    csilk_json_t* r =
        csilk_db_query_param_json(pool,
                                  "INSERT INTO ai_settings (id, config_json, updated_at) "
                                  "VALUES (1, ?, CURRENT_TIMESTAMP) "
                                  "ON CONFLICT(id) DO UPDATE SET config_json=excluded.config_json, "
                                  "updated_at=CURRENT_TIMESTAMP",
                                  (const char*[]){config_json, NULL});
    int ok = (r != NULL);
    csilk_json_free(r);
    return ok ? 0 : -1;
}
