#pragma once

/**
 * @file migration_engine.h
 * @brief 结构化、版本化数据库迁移引擎核心接口
 */

#include "infrastructure/database/database.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  version;
    char name[128];
    char filepath[512];
    char checksum[65];
    bool is_applied;
    int  execution_time_ms;
} mf_migration_item_t;

typedef struct mf_migration_engine_s mf_migration_engine_t;

/**
 * @brief 创建迁移引擎实例
 * @param db 数据库抽象句柄
 * @param migrations_base_dir 迁移根目录（包含 sqlite/ 与 postgres/ 子目录）
 * @param out_engine 输出引擎实例指针
 * @return 0 成功，-1 失败
 */
int mf_migration_engine_new(mf_db_t*                db,
                            const char*             migrations_base_dir,
                            mf_migration_engine_t** out_engine);

/**
 * @brief 释放迁移引擎实例
 */
void mf_migration_engine_free(mf_migration_engine_t* engine);

/**
 * @brief 发现并解析所有迁移文件（按版本号升序排列）
 * @param engine 引擎实例
 * @param out_items 输出条目数组指针（由调用者通过 free 释放）
 * @param out_count 输出条目数量
 * @return 0 成功，-1 失败
 */
int mf_migration_discover(mf_migration_engine_t* engine,
                          mf_migration_item_t**  out_items,
                          int*                   out_count);

/**
 * @brief 校验已应用的迁移文件校验和是否被篡改
 * @param engine 引擎实例
 * @return 0 校验通过，-1 校验失败（存在篡改或文件缺失）
 */
int mf_migration_validate(mf_migration_engine_t* engine);

/**
 * @brief 运行未应用的迁移脚本（支持存量生产库自动基线化，每个脚本在独立事务中执行）
 * @param engine 引擎实例
 * @param out_applied_count 输出成功应用的迁移数量
 * @return 0 成功，-1 失败
 */
int mf_migration_apply(mf_migration_engine_t* engine, int* out_applied_count);

/**
 * @brief 获取全部迁移项的状态清单
 * @param engine 引擎实例
 * @param out_items 输出条目数组指针（由调用者通过 free 释放）
 * @param out_count 输出条目数量
 * @return 0 成功，-1 失败
 */
int
mf_migration_status(mf_migration_engine_t* engine, mf_migration_item_t** out_items, int* out_count);

#ifdef __cplusplus
}
#endif
