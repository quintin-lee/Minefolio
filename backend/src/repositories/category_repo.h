#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file category_repo.h
 * @brief 收支与资产分类 (Categories) 数据访问层接口定义
 *
 * 提供多层级分类树（包含支出 expense、收入 income、资产/负债 asset 等类型）
 * 的 CRUD 数据持久化、子分类级联检查、按需查找或创建 (find-or-create) 等数据访问操作。
 */

/**
 * @brief 查询指定用户的分类列表，支持按大类类型筛选
 *
 * 结果集按 `sort_order ASC, name ASC` 排序。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param type 可选分类类别（如 "expense", "income", "asset"），为 NULL 或空字符串时查询所有分类
 * @return csilk_json_t* 包含分类对象的 JSON 数组
 */
csilk_json_t* category_list(csilk_db_pool_t* pool, int64_t user_id, const char* type);

/**
 * @brief 根据分类 ID 获取单个分类的详细属性
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（租户隔离验证）
 * @param id 分类 ID
 * @return csilk_json_t* 包含单个分类对象的 JSON 数组（长度为 1），未找到返回 NULL
 */
csilk_json_t* category_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 插入新的分类记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 分类显示名称
 * @param parent_id 父分类 ID（顶级分类传入 0）
 * @param type 分类主类别 ("expense", "income", "asset" 等)
 * @param asset_type 资产细分子类型 (如 "cash", "bank", "stock", "fund", "loan" 等)
 * @param currency 预设货币代码（可选）
 * @param icon 图标名称标识符（如 Element Plus 图标名）
 * @param sort_order 排序权重序号
 * @return int64_t 成功时返回新创建的分类 ID，失败返回 0
 */
int64_t category_insert(csilk_db_pool_t* pool,
                        int64_t          user_id,
                        const char*      name,
                        int64_t          parent_id,
                        const char*      type,
                        const char*      asset_type,
                        const char*      currency,
                        const char*      icon,
                        int              sort_order);

/**
 * @brief 更新分类的基础属性与排序
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 目标分类 ID
 * @param name 分类名称
 * @param type 分类主类型
 * @param asset_type 资产细分类型
 * @param currency 货币代码
 * @param icon 图标标识符
 * @param sort_order 排序权重
 * @return int 成功更新返回 1，否则返回 0
 */
int category_update(csilk_db_pool_t* pool,
                    int64_t          user_id,
                    int64_t          id,
                    const char*      name,
                    const char*      type,
                    const char*      asset_type,
                    const char*      currency,
                    const char*      icon,
                    int              sort_order);

/**
 * @brief 删除指定分类记录
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 目标分类 ID
 * @return int 成功删除返回 1，否则返回 0
 */
int category_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 统计指定分类下的直接子分类数量
 *
 * 用于在删除或转移分类前进行前置校验，防止孤儿节点产生。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param parent_id 父分类 ID
 * @return csilk_json_t* 包含 `cnt` 字段计数的 JSON 数组
 */
csilk_json_t* category_children(csilk_db_pool_t* pool, int64_t user_id, int64_t parent_id);

/**
 * @brief 查找同名同层级的分类，若不存在则自动创建
 *
 * 常用在账单 CSV 批量导入、规则自动映射或 OCR 识别场景中。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param name 分类名称
 * @param parent_id 父分类 ID (0 表示顶级分类)
 * @param type 主类别 ("expense", "income", "asset")
 * @param asset_type 资产细分子类型
 * @param icon 图标名称
 * @param sort_order 排序权重
 * @return int64_t 已存在的分类 ID 或新创建的分类 ID
 */
int64_t category_find_or_create(csilk_db_pool_t* pool,
                                int64_t          user_id,
                                const char*      name,
                                int64_t          parent_id,
                                const char*      type,
                                const char*      asset_type,
                                const char*      icon,
                                int              sort_order);

/**
 * @brief 校验分类是否存在且属于当前用户
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 分类 ID
 * @return int 存在返回 1，不存在返回 0
 */
int category_exists(csilk_db_pool_t* pool, int64_t user_id, int64_t id);
