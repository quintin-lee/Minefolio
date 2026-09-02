#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file tag_repo.h
 * @brief 自定义业务标签 (Tags) 数据访问层接口定义
 *
 * 负责用户级交易与收支标签 (tags: id, name, color) 的 CRUD 持久化、
 * 输入前缀模糊自动补全提示 (suggestions) 等数据访问方法。
 */

/**
 * @brief 查询指定用户的所有标签列表
 *
 * 按标签名称正序 (`ORDER BY name ASC`) 返回。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含标签对象的 JSON 数组
 */
csilk_json_t* tag_list(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据用户输入的前缀/关键字获取标签自动补全建议列表
 *
 * 当 prefix 为空时默认返回前 20 条，否则按 `name LIKE %prefix%` 模糊匹配最多 10 条建议。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param prefix 用户输入的搜索关键词（可选）
 * @return csilk_json_t* 包含 id, name, color 的建议列表 JSON 数组
 */
csilk_json_t* tag_suggestions(csilk_db_pool_t* pool, int64_t user_id, const char* prefix);

/**
 * @brief 创建新的自定义标签
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 标签显示名称
 * @param color 标签背景色值（如 "#666666"）
 * @return int64_t 成功返回新标签主键 ID，失败返回 0
 */
int64_t tag_insert(csilk_db_pool_t* pool, int64_t user_id, const char* name, const char* color);

/**
 * @brief 更新指定标签的名称与颜色
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 标签 ID
 * @param name 新名称
 * @param color 新色值
 * @return int 成功返回 1，失败返回 0
 */
int
tag_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const char* name, const char* color);

/**
 * @brief 删除指定标签记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 标签 ID
 * @return int 成功删除返回 1，失败返回 0
 */
int tag_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
