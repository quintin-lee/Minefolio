/**
 * @file import_rule_controller.c
 * @brief 账单导入自动分类匹配规则控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器定义薄包装路由处理函数（Thin Handler Wrappers），
 * 将 HTTP 请求直接分发至 services/import_rule_service.c 中的业务逻辑层。
 */

#include "controllers/import_rule_controller.h"
#include "services/import_rule_service.h"

/**
 * @brief 处理获取导入匹配规则列表请求
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/import-rules
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": [{"id": 1, "pattern": "美团", "match_type": "contains", "category_id": 10, "priority": 100}, ...]}
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_list(csilk_ctx_t* c)
{
    import_rule_service_list(c);
}

/**
 * @brief 处理获取单个规则详情请求
 *
 * @details HTTP 方法: GET
 *          REST 路径: /api/import-rules/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 规则 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 404 Not Found: 规则不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_get(csilk_ctx_t* c)
{
    import_rule_service_get(c);
}

/**
 * @brief 处理创建导入规则请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/import-rules
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体 (JSON):
 *          - pattern: 匹配表达式/关键字 (string, 必填)
 *          - match_type: 匹配模式 (string, 可选, "contains" | "regex" | "exact", 默认 "contains")
 *          - category_id: 自动关联分类 ID (int64, 必填)
 *          - tag_id: 自动关联标签 ID (int64, 可选)
 *          - priority: 优先级序号 (int, 可选, 默认 0, 越大约先匹配)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"id": 1, ...}}
 *          - 400 Bad Request: 参数不合法 (code: 1002)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_create(csilk_ctx_t* c)
{
    import_rule_service_create(c);
}

/**
 * @brief 处理更新导入规则请求
 *
 * @details HTTP 方法: PUT
 *          REST 路径: /api/import-rules/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 规则 ID (int64)
 *          请求体 (JSON):
 *          - pattern: 新匹配文本 (string, 可选)
 *          - match_type: 新匹配类型 (string, 可选)
 *          - category_id: 新分类 ID (int64, 可选)
 *          - tag_id: 新标签 ID (int64, 可选)
 *          - priority: 新优先级 (int, 可选)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 规则不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_update(csilk_ctx_t* c)
{
    import_rule_service_update(c);
}

/**
 * @brief 处理删除导入规则请求
 *
 * @details HTTP 方法: DELETE
 *          REST 路径: /api/import-rules/:id
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          路径参数 (Path Parameters):
 *          - id: 待删除规则 ID (int64)
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *          - 404 Not Found: 规则不存在 (code: 1003)
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_delete(csilk_ctx_t* c)
{
    import_rule_service_delete(c);
}

/**
 * @brief 处理重置预设默认规则请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/import-rules/reset-defaults
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求参数: 无
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": null}
 *
 *          清空用户自定义规则并重新生成一组系统内置的商户分类映射模板。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
static void
handle_reset_defaults(csilk_ctx_t* c)
{
    import_rule_service_reset_defaults(c);
}

void
register_import_rule_routes(csilk_app_t* app)
{
    csilk_app_get(app, "/api/import-rules", handle_list);
    csilk_app_post(app, "/api/import-rules", handle_create);
    csilk_app_post(app, "/api/import-rules/reset-defaults", handle_reset_defaults);
    csilk_app_get(app, "/api/import-rules/:id", handle_get);
    csilk_app_put(app, "/api/import-rules/:id", handle_update);
    csilk_app_delete(app, "/api/import-rules/:id", handle_delete);
}
