#pragma once

#include "csilk/csilk.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"

int auth_usecase_register(void* pool, const register_user_cmd_t* cmd, csilk_ctx_t* c,
                          csilk_json_t** out_data, auth_usecase_result_t* out_res);

int auth_usecase_login(void* pool, const login_user_cmd_t* cmd, csilk_ctx_t* c,
                       csilk_json_t** out_data, auth_usecase_result_t* out_res);

int auth_usecase_me(void* pool, int64_t user_id, csilk_json_t** out_data,
                    auth_usecase_result_t* out_res);

int auth_usecase_change_password(void* pool, const change_password_cmd_t* cmd, csilk_ctx_t* c,
                                auth_usecase_result_t* out_res);

int auth_usecase_2fa_status(void* pool, int64_t user_id, csilk_json_t** out_data,
                            auth_usecase_result_t* out_res);

int auth_usecase_2fa_setup(void* pool, int64_t user_id, csilk_json_t** out_data,
                           auth_usecase_result_t* out_res);

int auth_usecase_2fa_enable(void* pool, const enable_2fa_cmd_t* cmd, csilk_json_t** out_data,
                            auth_usecase_result_t* out_res);

int auth_usecase_2fa_disable(void* pool, int64_t user_id, auth_usecase_result_t* out_res);

int auth_usecase_2fa_verify_login(void* pool, const verify_2fa_login_cmd_t* cmd,
                                  csilk_json_t** out_data, auth_usecase_result_t* out_res);

int auth_usecase_oauth_providers(csilk_json_t** out_data, auth_usecase_result_t* out_res);

int auth_usecase_oauth_callback(void* pool, const oauth_callback_cmd_t* cmd,
                                csilk_json_t** out_data, auth_usecase_result_t* out_res);
