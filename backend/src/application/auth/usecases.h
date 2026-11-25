#pragma once

#include "csilk/csilk.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"

int auth_usecase_register(void*                      pool,
                          const register_user_cmd_t* cmd,
                          csilk_ctx_t*               c,
                          csilk_json_t**             out_data,
                          auth_usecase_result_t*     out_res);

int auth_usecase_login(void*                   pool,
                       const login_user_cmd_t* cmd,
                       csilk_ctx_t*            c,
                       csilk_json_t**          out_data,
                       auth_usecase_result_t*  out_res);

int auth_usecase_me(void*                  pool,
                    int64_t                user_id,
                    csilk_json_t**         out_data,
                    auth_usecase_result_t* out_res);

int auth_usecase_change_password(void*                        pool,
                                 const change_password_cmd_t* cmd,
                                 csilk_ctx_t*                 c,
                                 auth_usecase_result_t*       out_res);

int auth_usecase_2fa_status(void*                  pool,
                            int64_t                user_id,
                            csilk_json_t**         out_data,
                            auth_usecase_result_t* out_res);

int auth_usecase_2fa_setup(void*                  pool,
                           int64_t                user_id,
                           csilk_json_t**         out_data,
                           auth_usecase_result_t* out_res);

int auth_usecase_2fa_enable(void*                   pool,
                            const enable_2fa_cmd_t* cmd,
                            csilk_json_t**          out_data,
                            auth_usecase_result_t*  out_res);

int auth_usecase_2fa_disable(void*                    pool,
                             const disable_2fa_cmd_t* cmd,
                             csilk_ctx_t*             c,
                             auth_usecase_result_t*   out_res);

int auth_usecase_2fa_verify_login(void*                         pool,
                                  const verify_2fa_login_cmd_t* cmd,
                                  csilk_json_t**                out_data,
                                  auth_usecase_result_t*        out_res);

int auth_usecase_oauth_providers(csilk_json_t** out_data, auth_usecase_result_t* out_res);

int auth_usecase_oauth_callback(void*                       pool,
                                const oauth_callback_cmd_t* cmd,
                                csilk_json_t**              out_data,
                                auth_usecase_result_t*      out_res);

/**
 * @brief 重新固化 OAuth mock 开关（仅测试使用）
 * @note mock 开关在启动期从 MINEFOLIO_TEST_OAUTH_MOCK 固化一次；
 *       该函数供测试在改变环境变量后重新固化，模拟不同启动环境。
 */
void auth_oauth_mock_reset(void);

/**
 * @brief 判断指定用户是否为实例管理员（最早注册账号）
 */
int auth_usecase_is_instance_admin(void* pool, int64_t user_id);

/**
 * @brief 获取实例管理员用户 ID
 */
int64_t auth_usecase_get_instance_admin_id(void* pool);

/**
 * @brief 获取系统已注册用户总数
 */
int auth_usecase_user_count(void* pool);

/**
 * @brief 验证登录密码，并在匹配 csilk < v1.2.0 遗留哈希时透明升级为当前 bcrypt 哈希
 * @param pool         数据库连接池（可为 NULL；当 pool != NULL 且 user_id > 0 时回写升级后哈希）
 * @param user_id      用户 ID
 * @param password     明文密码
 * @param password_len 明文密码字节长度
 * @param stored_hash  数据库存储的 bcrypt 哈希
 * @return 0 验证通过；-1 不匹配或参数非法
 */
int auth_usecase_verify_and_upgrade_password(void*       pool,
                                             int64_t     user_id,
                                             const char* password,
                                             size_t      password_len,
                                             const char* stored_hash);

/**
 * @brief 首次初始化创建管理员账号并播种默认数据（事务内完成）
 */
int auth_usecase_setup_first_admin(void*                  pool,
                                   csilk_ctx_t*           c,
                                   const char*            username,
                                   const char*            password_enc,
                                   csilk_json_t**         out_data,
                                   auth_usecase_result_t* out_res);
