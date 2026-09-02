/**
 * @file ledger_controller.c
 * @brief 多账本空间与协作成员权限（RBAC）控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器负责将 HTTP 请求路由映射并分发至
 * services/ledger_service.c 中实现的多账本及基于角色的权限控制业务层。
 */

#include "controllers/ledger_controller.h"
#include "services/ledger_service.h"

/**
 * @brief 注册多账本与协作权限管理模块的所有 HTTP 路由
 *
 * @details 详细端点定义与参数说明：
 *
 * 1. GET /api/ledgers
 *    - 功能: 获取当前用户拥有或参与的所有账本列表
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [{"id": 1, "name": "家庭账本", "description": "...", "currency": "CNY", "is_default": 1, "role": "owner", "member_count": 3}, ...]}
 *
 * 2. POST /api/ledgers
 *    - 功能: 创建新账本空间
 *    - 鉴权: JWT (Bearer Token)
 *    - 请求体: {"name": "旅行专项账本", "description": "日本游记账", "currency": "JPY"}
 *    - 响应: 200 OK {"code": 0, "data": {"id": 2, ...}}
 *
 * 3. GET /api/ledgers/:id
 *    - 功能: 获取指定账本详情与当前用户的操作角色
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": {...}}
 *
 * 4. PUT /api/ledgers/:id
 *    - 功能: 修改账本元数据或设置为主账本
 *    - 鉴权: JWT (Bearer Token, 需 owner 角色)
 *    - 请求体: {"name": "新名称", "is_default": 1}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 5. DELETE /api/ledgers/:id
 *    - 功能: 删除账本及其关联数据
 *    - 鉴权: JWT (Bearer Token, 需 owner 角色且不能删除默认账本)
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 6. GET /api/ledgers/:id/members
 *    - 功能: 查看账本协作者成员列表及其角色 (owner / editor / viewer)
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": [{"user_id": 10, "username": "alice", "role": "editor", "joined_at": "..."}, ...]}
 *
 * 7. POST /api/ledgers/:id/members
 *    - 功能: 直接通过用户名添加协作者
 *    - 鉴权: JWT (Bearer Token, 需 owner 角色)
 *    - 请求体: {"username": "bob", "role": "editor"}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 8. PUT /api/ledgers/:id/members/:user_id
 *    - 功能: 调整协作者权限角色
 *    - 鉴权: JWT (Bearer Token, 需 owner 角色)
 *    - 请求体: {"role": "viewer"}
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 9. DELETE /api/ledgers/:id/members/:user_id
 *    - 功能: 移除协作者或普通成员主动退出账本
 *    - 鉴权: JWT (Bearer Token)
 *    - 响应: 200 OK {"code": 0, "data": null}
 *
 * 10. POST /api/ledgers/:id/invite-code
 *     - 功能: 生成或刷新账本 6 位加入邀请码（有效期可设置）
 *     - 鉴权: JWT (Bearer Token, 需 owner/editor 角色)
 *     - 响应: 200 OK {"code": 0, "data": {"invite_code": "839201", "expires_in": 86400}}
 *
 * 11. POST /api/ledgers/join
 *     - 功能: 通过 6 位邀请码加入账本
 *     - 鉴权: JWT (Bearer Token)
 *     - 请求体: {"invite_code": "839201"}
 *     - 响应: 200 OK {"code": 0, "data": {"ledger_id": 2, "name": "..."}}
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void
register_ledger_routes(csilk_app_t* app)

{
    csilk_app_get_ext(app,
                      "/api/ledgers",
                      ledger_service_list,
                      NULL,
                      NULL,
                      "List ledgers",
                      "Get all ledgers current user owns or belongs to");
    csilk_app_post_ext(app,
                       "/api/ledgers",
                       ledger_service_create,
                       NULL,
                       NULL,
                       "Create ledger",
                       "Create a new ledger");
    csilk_app_get_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_get,
                      NULL,
                      NULL,
                      "Get ledger",
                      "Get ledger details");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_update,
                      NULL,
                      NULL,
                      "Update ledger",
                      "Update ledger metadata");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id",
                         ledger_service_delete,
                         NULL,
                         NULL,
                         "Delete ledger",
                         "Delete ledger and associated items");

    csilk_app_get_ext(app,
                      "/api/ledgers/:id/members",
                      ledger_service_list_members,
                      NULL,
                      NULL,
                      "List members",
                      "Get ledger members and roles");
    csilk_app_post_ext(app,
                       "/api/ledgers/:id/members",
                       ledger_service_add_member,
                       NULL,
                       NULL,
                       "Add member",
                       "Add member by username");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id/members/:user_id",
                      ledger_service_update_member,
                      NULL,
                      NULL,
                      "Update member role",
                      "Update member role");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id/members/:user_id",
                         ledger_service_remove_member,
                         NULL,
                         NULL,
                         "Remove member",
                         "Remove member or leave ledger");

    csilk_app_post_ext(app,
                       "/api/ledgers/:id/invite-code",
                       ledger_service_create_invite_code,
                       NULL,
                       NULL,
                       "Generate invite code",
                       "Generate or refresh 6-digit invite code");
    csilk_app_post_ext(app,
                       "/api/ledgers/join",
                       ledger_service_join_by_invite,
                       NULL,
                       NULL,
                       "Join by invite code",
                       "Join a ledger using invite code");
}
