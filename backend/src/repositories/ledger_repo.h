#pragma once
#include "csilk/csilk.h"
#include "common/db.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @file ledger_repo.h
 * @brief 多账本空间 (Ledgers) 与成员权限 RBAC 数据访问层接口
 *
 * 负责多账本协同空间 (ledgers)、账本成员角色权限控制 (ledger_members: owner, editor, viewer)、
 * 默认账本自动兜底初始化、账本级联资源清理以及邀请码加入机制的数据访问层操作。
 */

/**
 * @brief 查询指定用户参与的所有账本列表
 *
 * 包含用户拥有的与受邀加入的账本，返回字段中内嵌用户的当前角色 (`my_role`)、
 * 账本成员总数 (`member_count`) 和账本内资产总估值 (`total_assets`)。
 * 会自动确保新用户具备至少一个默认账本。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含账本空间对象的 JSON 数组（按默认账本置顶、ID 升序排列）
 */
csilk_json_t* ledger_list_by_user(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据账本 ID 获取账本详细配置与统计信息
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @return csilk_json_t* 包含单条账本详情的 JSON 数组（长度为 1），未命中返回 NULL
 */
csilk_json_t* ledger_get(csilk_db_pool_t* pool, int64_t ledger_id);

/**
 * @brief 获取用户的默认账本 ID（若不存在则自动创建）
 *
 * 针对新注册用户或历史兼容场景，自动创建名为 "默认账本" 的记录并将其绑定为 owner。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return int64_t 默认账本的主键 ID
 */
int64_t ledger_get_default(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 创建新的账本空间并自动将创建者添加为 owner
 *
 * 1. 插入 `ledgers` 表。
 * 2. 自动在 `ledger_members` 表中写入创建者角色为 'owner'。
 *
 * @param pool 数据库连接池指针
 * @param owner_id 所有者用户 ID
 * @param name 账本名称
 * @param description 账本描述
 * @param currency 本位币种代码（如 "CNY"）
 * @param icon 图标标识（如 "ph:wallet"）
 * @param color 主题色值（十六进制色值，如 "#3b82f6"）
 * @param is_default 是否设为默认主账本
 * @return int64_t 成功返回新账本 ID，失败返回 0
 */
int64_t ledger_create(csilk_db_pool_t* pool,
                      int64_t          owner_id,
                      const char*      name,
                      const char*      description,
                      const char*      currency,
                      const char*      icon,
                      const char*      color,
                      bool             is_default);

/**
 * @brief 更新账本的基础元信息
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param name 账本名称
 * @param description 描述
 * @param currency 货币
 * @param icon 图标
 * @param color 颜色
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_update(csilk_db_pool_t* pool,
                  int64_t          ledger_id,
                  const char*      name,
                  const char*      description,
                  const char*      currency,
                  const char*      icon,
                  const char*      color);

/**
 * @brief 级联删除账本及其下属的所有关联业务数据
 *
 * 严格按照外键依赖顺序级联清理：
 * 1. dca_plans (定投计划)
 * 2. cashflow_schedules (现金流计划)
 * 3. daily_expenses (日常收支)
 * 4. transactions (交易记录)
 * 5. assets (资产账户)
 * 6. categories (分类)
 * 7. ledger_members (成员关联)
 * 8. ledgers (账本本身)
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 待删除账本 ID
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_delete(csilk_db_pool_t* pool, int64_t ledger_id);

/**
 * @brief 查询指定账本的所有成员列表及其角色
 *
 * 按照角色权限降序排列 (`owner` -> `editor` -> `viewer`)。
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @return csilk_json_t* 包含成员 user_id, username, role, joined_at 的 JSON 数组
 */
csilk_json_t* ledger_member_list(csilk_db_pool_t* pool, int64_t ledger_id);

/**
 * @brief 查询指定用户在特定账本中的权限角色
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 用户 ID
 * @param[out] out_role 接收角色名称的字符串缓冲区指针
 * @param out_len 缓冲区容量大小
 * @return const char* 角色字符串（如 "owner", "editor", "viewer"），若非成员返回 NULL
 */
const char* ledger_get_user_role(
    csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, char* out_role, size_t out_len);

/**
 * @brief 向账本添加新成员
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 待添加用户 ID
 * @param role 分配的角色 ("editor", "viewer" 等)
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_member_add(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id, const char* role);

/**
 * @brief 修改账本现有成员的权限角色
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 目标用户 ID
 * @param new_role 新角色 ("editor", "viewer" 等)
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_member_update_role(csilk_db_pool_t* pool,
                              int64_t          ledger_id,
                              int64_t          user_id,
                              const char*      new_role);

/**
 * @brief 从账本中移除指定成员
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param user_id 待移除用户 ID
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_member_remove(csilk_db_pool_t* pool, int64_t ledger_id, int64_t user_id);

/**
 * @brief 生成或刷新账本的邀请码与过期时间
 *
 * @param pool 数据库连接池指针
 * @param ledger_id 账本 ID
 * @param invite_code 随机生成的邀请码字符串
 * @param expires_at 过期时间戳字符串 (ISO 8601 格式)
 * @return int 成功返回 0，失败返回 -1
 */
int ledger_update_invite_code(csilk_db_pool_t* pool,
                              int64_t          ledger_id,
                              const char*      invite_code,
                              const char*      expires_at);

/**
 * @brief 根据有效邀请码查询账本基础信息
 *
 * 仅返回邀请码有效且未过期的账本记录。
 *
 * @param pool 数据库连接池指针
 * @param invite_code 待验证的邀请码字符串
 * @return csilk_json_t* 包含账本基本信息的 JSON 数组，若无效或已过期返回 NULL
 */
csilk_json_t* ledger_find_by_invite_code(csilk_db_pool_t* pool, const char* invite_code);
