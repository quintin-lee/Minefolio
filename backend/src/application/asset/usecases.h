#pragma once

#include <stdint.h>
#include "application/asset/commands.h"
#include "application/asset/dtos.h"
#include "csilk/csilk.h"

/**
 * @brief 创建资产用例编排
 */
int asset_usecase_create(void* pool, const create_asset_cmd_t* cmd, asset_usecase_result_t* out_res);

/**
 * @brief 更新资产基础属性或投资持仓用例编排
 */
int asset_usecase_update(void* pool, const update_asset_cmd_t* cmd, asset_usecase_result_t* out_res);

/**
 * @brief 删除资产用例编排
 */
int asset_usecase_delete(void* pool, const delete_asset_cmd_t* cmd, asset_usecase_result_t* out_res);

/**
 * @brief 查询单条资产详情及关联流水
 */
int asset_usecase_get(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_json);

/**
 * @brief 分页查询资产列表
 */
int asset_usecase_query(void* pool, const query_asset_filter_t* filter, csilk_json_t** out_list, int64_t* out_total);

/**
 * @brief 分页查询资产余额变动审计日志
 */
int asset_usecase_query_logs(void* pool, const query_asset_log_filter_t* filter, csilk_json_t** out_list, int64_t* out_total);

/**
 * @brief 单资产从底层交易事实重建重算
 */
int asset_usecase_rebuild_single(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data, asset_usecase_result_t* out_res);

/**
 * @brief 整个投资组合从底层交易事实全量重建重算
 */
int asset_usecase_rebuild_all(void* pool, int64_t user_id, asset_usecase_result_t* out_res);
