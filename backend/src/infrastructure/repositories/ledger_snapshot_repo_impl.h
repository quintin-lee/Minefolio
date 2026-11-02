#pragma once
#include "csilk/csilk.h"
#include "core/ledger/ledger_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 查询指定标的在给定日期前（或最新）的账本检查点快照
 *
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param asset_id 目标资产 ID
 * @param target_date 截止日期（如 "2026-09-02"，传 NULL 或空串表示无截止限制取最新）
 * @param out_snapshot 输出匹配的快照 JSON 对象（调用方负责 csilk_json_free），未找到时置 NULL
 * @return 0 成功（含未找到快照的情况）；<0 数据库或参数错误
 */
int ledger_snapshot_get_latest(csilk_db_pool_t* pool,
                               int64_t          user_id,
                               int64_t          asset_id,
                               const char*      target_date,
                               csilk_json_t**   out_snapshot);

/**
 * @brief 保存或更新指定标的的账本检查点快照
 *
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param asset_id 目标资产 ID
 * @param snapshot_date 快照归属业务日期 (如 "2026-09-02")
 * @param pos 投资标的持仓状态 (可选，NULL 时各字段记 0)
 * @param balance 现金/负债账户余额
 * @param checkpoint_date 关联的最新交易时间 (NULL 或空串时取 snapshot_date)
 * @param checkpoint_id 关联的最新交易 ID
 * @param lots_json 批次状态 JSON 文本 (可选，NULL 时置 NULL)
 * @return 0 成功；<0 失败
 */
int ledger_snapshot_save(csilk_db_pool_t*               pool,
                         int64_t                        user_id,
                         int64_t                        asset_id,
                         const char*                    snapshot_date,
                         const ledger_position_state_t* pos,
                         money_t                        balance,
                         const char*                    checkpoint_date,
                         int64_t                        checkpoint_id,
                         const char*                    lots_json);

/**
 * @brief 失效/删除指定标的在给定日期及之后的所有账本检查点快照
 *
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param asset_id 目标资产 ID
 * @param from_date 起始业务时间或日期 (如 "2026-09-01" 或 "2026-09-01 15:00:00")
 * @return 0 成功；<0 失败
 */
int ledger_snapshot_invalidate_from(csilk_db_pool_t* pool,
                                    int64_t          user_id,
                                    int64_t          asset_id,
                                    const char*      from_date);

#ifdef __cplusplus
}
#endif
