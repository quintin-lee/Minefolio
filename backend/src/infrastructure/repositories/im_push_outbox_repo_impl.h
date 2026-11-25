#pragma once

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 入队一条微信 iLink 推送记录，返回 push_id (>0) 或 -1。 */
int64_t mf_im_push_outbox_enqueue(csilk_db_pool_t* pool,
                                  int64_t          instance_id,
                                  int64_t          mf_user_id,
                                  int64_t          job_run_log_id,
                                  const char*      kind,
                                  const char*      title,
                                  const char*      content,
                                  int64_t          ttl_sec,
                                  int              max_attempts);

/** 查询单条 Outbox 记录（不存在返回 NULL）。 */
csilk_json_t* mf_im_push_outbox_get(csilk_db_pool_t* pool, int64_t push_id);

/** 将未终态的测试推送标为 expired（避免同步等待超时后补推）。 */
int
mf_im_push_outbox_mark_expired_by_id(csilk_db_pool_t* pool, int64_t push_id, const char* reason);

/**
 * @brief 将超期未投递的 pending/leased 记录批量置为 expired，返回去重的关联 job_run_log_id 列表。
 * @param instance_id_or_zero >0 时仅扫描该实例；0 时全局扫描。
 */
int mf_im_push_outbox_expire_stale(csilk_db_pool_t* pool,
                                   int64_t          instance_id_or_zero,
                                   int64_t          now_ts,
                                   const char*      default_reason,
                                   int64_t**        out_log_ids,
                                   size_t*          out_log_count);

/**
 * @brief 原子回收超时租约并领取待发推送，返回 JSON 数组。
 * @param has_context false 时仅领取 kind='test'，保留 'scheduler' 等待唤醒。
 */
csilk_json_t* mf_im_push_outbox_claim(csilk_db_pool_t* pool,
                                      int64_t          instance_id,
                                      bool             has_context,
                                      int              limit,
                                      int64_t          now_ts,
                                      int64_t          lease_sec);

/**
 * @brief 网关回执推进 Outbox 状态。
 * @return 0 成功或已终态幂等，-3 不存在/跨实例，-2 参数非法，-1 数据库错误。
 */
int mf_im_push_outbox_ack(csilk_db_pool_t* pool,
                          int64_t          instance_id,
                          int64_t          push_id,
                          const char*      result,
                          const char*      error_msg,
                          int              retry_after_sec,
                          int64_t          now_ts,
                          int64_t*         out_job_run_log_id);

/** 根据 sync 计数与关联 Outbox 状态聚合更新 job_run_logs.notification_status。 */
int mf_im_push_outbox_rollup_job_log(csilk_db_pool_t* pool, int64_t job_run_log_id);

#ifdef __cplusplus
}
#endif
