#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

/**
 * @file import_rule_repo.h
 * @brief 账单导入自动分类规则 (Import Rules) 数据访问层接口定义
 *
 * 负责 CSV 账单、微信/支付宝流水导入及 OCR 识别场景下的关键词自动匹配与分类规则持久化。
 * 提供规则的 CRUD 操作以及新用户默认推荐分类规则库的自动播种初始化 (seed)。
 */

/**
 * @brief 查询指定用户的所有账单导入匹配规则列表
 *
 * 左连接 categories 表获取关联分类名称，按优先级与 ID 升序排列 (`ORDER BY r.priority ASC, r.id ASC`)。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @return csilk_json_t* 包含规则对象的 JSON 数组
 */
csilk_json_t* import_rule_list(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 根据规则 ID 获取单条导入匹配规则详情
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID（租户隔离验证）
 * @param id 规则 ID
 * @return csilk_json_t* 包含单条规则对象的 JSON 数组（长度为 1），未命中返回 NULL
 */
csilk_json_t* import_rule_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 插入一条新的导入分类规则
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param keyword 待匹配的关键词（如 "美团", "滴滴"）
 * @param match_field 匹配字段目标（"all", "note", "counterparty", "description" 等）
 * @param match_type 匹配方式（"contains", "exact", "regex", "startswith"）
 * @param category_id 命中后自动映射的目标分类 ID（可为 0 表示未指定）
 * @param target_type 交易收支目标类型 ("expense", "income")
 * @param priority 匹配优先级权重（数值越小优先级越高）
 * @param is_active 是否启用该规则 (1=启用, 0=停用)
 * @return int64_t 成功返回新规则主键 ID，失败返回 0
 */
int64_t import_rule_insert(csilk_db_pool_t* pool,
                           int64_t          user_id,
                           const char*      keyword,
                           const char*      match_field,
                           const char*      match_type,
                           int64_t          category_id,
                           const char*      target_type,
                           int              priority,
                           int              is_active);

/**
 * @brief 更新指定的导入匹配规则
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 规则 ID
 * @param keyword 匹配关键词
 * @param match_field 匹配字段
 * @param match_type 匹配方式
 * @param category_id 目标分类 ID
 * @param target_type 收支类型
 * @param priority 优先级权重
 * @param is_active 启用状态
 * @return int 成功返回 1，失败返回 0
 */
int import_rule_update(csilk_db_pool_t* pool,
                       int64_t          user_id,
                       int64_t          id,
                       const char*      keyword,
                       const char*      match_field,
                       const char*      match_type,
                       int64_t          category_id,
                       const char*      target_type,
                       int              priority,
                       int              is_active);

/**
 * @brief 删除指定的导入规则
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 * @param id 规则 ID
 * @return int 成功返回 1，失败返回 0
 */
int import_rule_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id);

/**
 * @brief 为新注册用户批量预置常用生活消费与收入的默认导入规则
 *
 * 覆盖餐饮（美团/饿了么/星巴克等）、交通（滴滴/铁路12306/加油等）、
 * 购物（京东/淘宝/盒马等）、生活缴费与工资薪酬等常见商户/场景。
 *
 * @param pool 数据库连接池指针
 * @param user_id 用户 ID
 */
void import_rule_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
