#pragma once

/**
 * @file repository.h
 * @brief 标签仓储抽象契约接口 (Domain Tag Repository Contract)
 *
 * 纯 C 契约，入参出参仅允许领域实体与标量数值，严禁返回 JSON 节点或编写 SQL。
 * 由 infrastructure/repositories/tag_repo_impl.c 实现。
 */

#include <stdint.h>
#include <stddef.h>
#include "domain/tag/entity.h"

/**
 * @brief 查询指定用户的所有标签
 * @return 0: 成功, -1: 数据库错误
 */
int mf_tag_repo_list(void* db_pool, int64_t user_id, mf_tag_t** out_list, size_t* out_count);

/**
 * @brief 根据 ID 查询单条标签
 * @return 0: 成功查到, 1: 不存在, -1: 数据库错误
 */
int mf_tag_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_tag_t* out);

/**
 * @brief 按前缀模糊搜索标签（自动补全建议）
 * @return 0: 成功, -1: 数据库错误
 */
int mf_tag_repo_suggestions(
    void* db_pool, int64_t user_id, const char* prefix, mf_tag_t** out_list, size_t* out_count);

/**
 * @brief 持久化保存新标签
 * @return 0: 成功, -1: 失败
 */
int mf_tag_repo_create(
    void* db_pool, int64_t user_id, const char* name, const char* color, int64_t* out_id);

/**
 * @brief 更新标签名称与颜色
 * @return 0: 成功, 1: 不存在, -1: 数据库错误
 */
int
mf_tag_repo_update(void* db_pool, int64_t user_id, int64_t id, const char* name, const char* color);

/**
 * @brief 删除指定标签
 * @return 0: 成功, 1: 不存在, -1: 数据库错误
 */
int mf_tag_repo_delete(void* db_pool, int64_t user_id, int64_t id);

/**
 * @brief 释放由仓储分配的实体数组内存
 */
void mf_tag_repo_free_list(mf_tag_t* list, size_t count);
