/**
 * @file import_rule_controller.h
 * @brief 账单导入自动分类匹配规则控制器头文件
 *
 * 声明对账单导入关键词匹配规则的增删改查、详情查看、重置默认规则
 * 以及路由注册函数。支持导入账单时通过正则/包含规则自动将交易归类到预设分类与标签。
 */

#pragma once
#include "csilk/csilk.h"
#include "csilk/app/app.h"

/**
 * @brief 注册对账单导入匹配规则相关的所有 HTTP 路由
 *
 * @details 注册包括规则列表、单条详情、新增规则、修改规则、删除规则及恢复默认规则等端点：
 *          - GET    /api/import-rules: 获取匹配规则列表
 *          - POST   /api/import-rules: 新增匹配规则
 *          - POST   /api/import-rules/reset-defaults: 重置为系统默认规则
 *          - GET    /api/import-rules/:id: 获取规则详情
 *          - PUT    /api/import-rules/:id: 更新规则
 *          - DELETE /api/import-rules/:id: 删除规则
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_import_rule_routes(csilk_app_t* app);
