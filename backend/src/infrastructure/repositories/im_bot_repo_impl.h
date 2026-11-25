#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IM 机器人绑定关系与 iLink 实例仓储
 *
 * im_bot_bindings 将 IM 平台账号映射到 Minefolio 用户；im_bot_instances 管理
 * 每用户独立的 iLink 机器人实例（凭证不上库，仅网关 state 090600）。
 */

/** 查询 (platform, im_user_id) 的绑定，返回 mf_user_id (>0) 或 0 未绑定。 */
int64_t mf_im_bot_repo_lookup(csilk_db_pool_t* pool,
                              const char*      platform,
                              const char*      im_user_id,
                              char*            scope_out,
                              size_t           scope_sz);

/** upsert 绑定：同 (platform, im_user_id) 已存在则更新 mf_user_id/scope/enabled。 */
int mf_im_bot_repo_upsert(csilk_db_pool_t* pool,
                          const char*      platform,
                          const char*      im_user_id,
                          int64_t          mf_user_id,
                          const char*      scope);

/** 列出某用户的全部 IM 绑定。返回 JSON 数组（可能为空）。 */
csilk_json_t* mf_im_bot_repo_list_for_user(csilk_db_pool_t* pool, int64_t mf_user_id);

/** 解绑：删除 (platform, im_user_id) 且属于 mf_user_id 的记录。 */
int mf_im_bot_repo_unbind(csilk_db_pool_t* pool,
                          const char*      platform,
                          const char*      im_user_id,
                          int64_t          mf_user_id);

/* ---- iLink 实例（用户级隔离） ---- */

/** 列出 owner 的实例（JSON 数组，隐藏 pending_verify_code）。 */
csilk_json_t* mf_im_bot_instance_list(csilk_db_pool_t* pool, int64_t user_id);

/** owner 查询单个实例；不属于 owner 返回 NULL。 */
csilk_json_t* mf_im_bot_instance_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/** 网关按 id 查询实例；不存在返回 NULL。 */
csilk_json_t* mf_im_bot_instance_get_any(csilk_db_pool_t* pool, int64_t id);

/** 网关列出全部实例；失败返回空 JSON 数组。 */
csilk_json_t* mf_im_bot_instance_list_all(csilk_db_pool_t* pool);

/** 创建实例；已存在返回 -2，其他失败 -1。 */
int64_t mf_im_bot_instance_create(csilk_db_pool_t* pool,
                                  int64_t          user_id,
                                  const char*      platform,
                                  const char*      adapter);

/** 重新登录：epoch+1、清二维码/验证码、状态 pending_login。 */
int mf_im_bot_instance_relogin(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/** 启用/停用实例；启用时 epoch+1 并回到 pending_login。 */
int mf_im_bot_instance_set_enabled(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled);

/** 删除实例并清理其 IM 绑定（事务）。 */
int mf_im_bot_instance_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/** 提交 owner 的手机验证码；成功 0，参数格式错误 -2，不存在/状态不符 -3，次数超限 -4，其他 -1。 */
int mf_im_bot_instance_submit_verify_code(csilk_db_pool_t* pool,
                                          int64_t          user_id,
                                          int64_t          id,
                                          const char*      code);

/** 网关消费一次性验证码：有有效码返回 1，无返回 0，失败 -1。 */
int mf_im_bot_instance_consume_verify_code(csilk_db_pool_t* pool,
                                           int64_t          id,
                                           char*            out,
                                           size_t           out_cap);

/** 网关上报扫码/错误状态；空串表示 SQL NULL。 */
int mf_im_bot_instance_update_status(csilk_db_pool_t* pool,
                                     int64_t          id,
                                     const char*      status,
                                     const char*      qr_content,
                                     const char*      qr_expires_at,
                                     const char*      last_error);

/** 确认登录：事务内校验冲突、upsert 绑定、激活实例。0 成功，-2 实例冲突，-3 绑定抢占。 */
int mf_im_bot_instance_activate(csilk_db_pool_t* pool,
                                int64_t          id,
                                const char*      ilink_bot_id,
                                const char*      ilink_user_id,
                                char*            err_buf,
                                size_t           err_cap);

/** 心跳：返回 status/epoch/enabled 并 touch last_seen_at。 */
int mf_im_bot_instance_heartbeat(csilk_db_pool_t* pool,
                                 int64_t          id,
                                 char*            status_out,
                                 size_t           status_cap,
                                 int64_t*         epoch_out,
                                 int*             enabled_out);

#ifdef __cplusplus
}
#endif
