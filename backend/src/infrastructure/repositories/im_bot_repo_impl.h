#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IM 机器人绑定关系仓储
 *
 * 将 IM 平台账号 (platform + im_user_id) 映射到 Minefolio 用户 (mf_user_id)。
 * 供 IM 查账机器人做身份锚点与能力收敛 (scope='readonly')。
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

#ifdef __cplusplus
}
#endif
