#pragma once

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 校验用户 iLink 实例在线后入队推送；非 active/disabled 时返回明确中文错误并 return -1。 */
int im_push_usecase_enqueue(void*       pool,
                            int64_t     user_id,
                            int64_t     job_run_log_id,
                            const char* kind,
                            const char* title,
                            const char* content,
                            int64_t     ttl_sec,
                            int64_t*    out_push_id,
                            char*       err_buf,
                            size_t      err_cap);

/** 网关按实例领取到期推送，自动清理过期与失效租约，并可选刷新 last_inbound_at。 */
int im_push_usecase_gateway_claim(void*          pool,
                                  int64_t        instance_id,
                                  bool           has_context,
                                  const char*    context_updated_at,
                                  int            limit,
                                  csilk_json_t** out_items,
                                  int*           code,
                                  char*          err,
                                  size_t         err_cap);

/** 网关回执单条推送并触发关联日志状态聚合。 */
int im_push_usecase_gateway_ack(void*       pool,
                                int64_t     instance_id,
                                int64_t     push_id,
                                const char* result,
                                const char* error_msg,
                                int         retry_after_sec,
                                int*        code,
                                char*       err,
                                size_t      err_cap);

/** 全局过期扫描并将关联日志更新为 failed / partial。 */
int im_push_usecase_expire_stale(void* pool);

/** 读取单条 Outbox 详情（用于通道测试同步轮询）。 */
csilk_json_t* im_push_usecase_get(void* pool, int64_t push_id);

/** 将等待超时的测试推送置为 expired。 */
int im_push_usecase_cancel_expired_test(void* pool, int64_t push_id, const char* reason);

/** 构造脱敏的 iLink 就绪态 JSON（给 GET /api/scheduler/channel 与 dashboard 使用）。 */
csilk_json_t* im_push_usecase_build_ilink_status(void* pool, int64_t user_id);

#ifdef __cplusplus
}
#endif
