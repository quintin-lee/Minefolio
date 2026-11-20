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
