#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @file import_rule_service.h
 * @brief 账单智能自动分类匹配规则（按交易对手方、描述关键词匹配分类）管理服务
 */

/**
 * @brief 分页查询用户的导入分类匹配规则列表 (GET /api/import-rules)
 * @param c HTTP 上下文
 */
void import_rule_service_list(csilk_ctx_t* c);

/**
 * @brief 获取单个导入规则详情 (GET /api/import-rules/:id)
 * @param c HTTP 上下文
 */
void import_rule_service_get(csilk_ctx_t* c);

/**
 * @brief 创建自定义导入分类匹配规则 (POST /api/import-rules)
 * @param c HTTP 上下文
 */
void import_rule_service_create(csilk_ctx_t* c);

/**
 * @brief 更新导入规则配置 (PUT /api/import-rules/:id)
 * @param c HTTP 上下文
 */
void import_rule_service_update(csilk_ctx_t* c);

/**
 * @brief 删除导入规则 (DELETE /api/import-rules/:id)
 * @param c HTTP 上下文
 */
void import_rule_service_delete(csilk_ctx_t* c);

/**
 * @brief 重置并恢复系统内置预设分类规则 (POST /api/import-rules/reset-defaults)
 * @param c HTTP 上下文
 */
void import_rule_service_reset_defaults(csilk_ctx_t* c);
