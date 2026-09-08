/**
 * @file usecases.h
 * @brief 账本用例接口声明 (Application Ledger Use Cases)
 */

#pragma once

#include "application/ledger/commands.h"
#include "application/ledger/dtos.h"
#include "csilk/csilk.h"
#include <stdint.h>

/**
 * @brief 列出用户所有账本
 */
csilk_json_t* ledger_usecase_list(void* pool, int64_t user_id);

/**
 * @brief 获取单个账本详情
 */
csilk_json_t* ledger_usecase_get(void*                    pool,
                                 int64_t                  user_id,
                                 int64_t                  ledger_id,
                                 ledger_usecase_result_t* out_res);

/**
 * @brief 创建新账本
 */
int64_t
ledger_usecase_create(void* pool, const create_ledger_cmd_t* cmd, ledger_usecase_result_t* out_res);

/**
 * @brief 更新账本元信息
 */
int
ledger_usecase_update(void* pool, const update_ledger_cmd_t* cmd, ledger_usecase_result_t* out_res);

/**
 * @brief 删除账本
 */
int ledger_usecase_delete(void*                    pool,
                          int64_t                  user_id,
                          int64_t                  ledger_id,
                          ledger_usecase_result_t* out_res);

/**
 * @brief 列出账本成员
 */
csilk_json_t* ledger_usecase_list_members(void*                    pool,
                                          int64_t                  user_id,
                                          int64_t                  ledger_id,
                                          ledger_usecase_result_t* out_res);

/**
 * @brief 添加账本成员
 */
int ledger_usecase_add_member(void*                    pool,
                              const add_member_cmd_t*  cmd,
                              ledger_usecase_result_t* out_res);

/**
 * @brief 更新成员角色
 */
int ledger_usecase_update_member(void*                      pool,
                                 const update_member_cmd_t* cmd,
                                 ledger_usecase_result_t*   out_res);

/**
 * @brief 移除账本成员
 */
int ledger_usecase_remove_member(void*                      pool,
                                 const remove_member_cmd_t* cmd,
                                 ledger_usecase_result_t*   out_res);

/**
 * @brief 生成邀请码
 */
int ledger_usecase_create_invite_code(void*                   pool,
                                      int64_t                 user_id,
                                      int64_t                 ledger_id,
                                      ledger_invite_result_t* out_res); /**
 * @brief 通过邀请码加入账本
 * @param[out] out_ledger_id 成功时返回加入的账本 ID
 */
int ledger_usecase_join_by_invite(void*                    pool,
                                  const join_ledger_cmd_t* cmd,
                                  ledger_usecase_result_t* out_res,
                                  int64_t*                 out_ledger_id);
