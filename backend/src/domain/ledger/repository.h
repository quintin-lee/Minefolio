/**
 * @file repository.h
 * @brief 账本领域仓储契约接口 (Domain Ledger Repository Contract)
 *
 * 纯 C 接口，零外部依赖。Infrastructure 层必须实现这些方法。
 */

#pragma once

#include "domain/ledger/entity.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 查询用户参与的所有账本（含聚合统计）
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_list_by_user(void*                   pool,
                                int64_t                 user_id,
                                mf_ledger_list_item_t** out_list,
                                size_t*                 out_count);

/**
 * @brief 根据 ID 获取账本详情（不含聚合统计）
 * @return 0: 成功查到, 1: 不存在, -1: 错误
 */
int mf_ledger_repo_get(void* pool, int64_t ledger_id, mf_ledger_t* out);

/**
 * @brief 获取用户的默认账本 ID（不存在则创建）
 */
int64_t mf_ledger_repo_get_or_create_default(void* pool, int64_t user_id);

/**
 * @brief 创建新账本并自动添加 owner
 * @return 新账本 ID，失败返回 0
 */
int64_t mf_ledger_repo_create(void* pool, int64_t owner_id, const mf_ledger_t* ledger);

/**
 * @brief 更新账本元信息
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_update(void* pool, int64_t ledger_id, const mf_ledger_t* ledger);

/**
 * @brief 级联删除账本及其所有关联数据
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_delete(void* pool, int64_t ledger_id);

/**
 * @brief 查询账本成员列表
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_member_list(void*                pool,
                               int64_t              ledger_id,
                               mf_ledger_member_t** out_list,
                               size_t*              out_count);

/**
 * @brief 获取用户在账本中的角色
 * @return 角色字符串指针，若不存在返回 NULL
 */
const char* mf_ledger_repo_get_user_role(
    void* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len);

/**
 * @brief 添加账本成员
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_member_add(void* pool, int64_t ledger_id, int64_t user_id, const char* role);

/**
 * @brief 更新成员角色
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_member_update_role(void*       pool,
                                      int64_t     ledger_id,
                                      int64_t     user_id,
                                      const char* new_role);

/**
 * @brief 移除账本成员
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_member_remove(void* pool, int64_t ledger_id, int64_t user_id);

/**
 * @brief 更新邀请码
 * @return 0: 成功, -1: 错误
 */
int mf_ledger_repo_update_invite_code(void*       pool,
                                      int64_t     ledger_id,
                                      const char* invite_code,
                                      const char* expires_at);

/**
 * @brief 根据邀请码查找账本
 * @return 0: 成功查到, 1: 未找到, -1: 错误
 */
int mf_ledger_repo_find_by_invite_code(void* pool, const char* invite_code, mf_ledger_t* out);

/**
 * @brief 根据用户名查找用户 ID
 * @return 用户 ID，未找到返回 0
 */
int64_t mf_ledger_repo_find_user_by_username(void* pool, const char* username);

/**
 * @brief 释放列表内存
 */
void mf_ledger_repo_free_list(void* list);
