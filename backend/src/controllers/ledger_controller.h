/**
 * @file ledger_controller.h
 * @brief 多账本空间与协作成员权限（RBAC）控制器头文件
 *
 * 声明多账本（Ledger）的创建与管理、默认账本切换、协作者成员管理（Owner/Editor/Viewer 权限控制）、
 * 6位邀请码生成与凭码加入账本相关的 HTTP 路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册多账本与协作权限管理模块的所有 HTTP 路由
 *
 * @details 注册账本 CRUD、成员权限控制与邀请加入端点：
 *          - GET    /api/ledgers: 获取用户所属的所有账本列表
 *          - POST   /api/ledgers: 创建新账本空间
 *          - GET    /api/ledgers/:id: 获取指定账本详情
 *          - PUT    /api/ledgers/:id: 更新账本元数据
 *          - DELETE /api/ledgers/:id: 删除账本（仅 Owner）
 *          - GET    /api/ledgers/:id/members: 获取账本成员与角色列表
 *          - POST   /api/ledgers/:id/members: 通过用户名直接添加协作者
 *          - PUT    /api/ledgers/:id/members/:user_id: 修改协作者角色 (editor/viewer)
 *          - DELETE /api/ledgers/:id/members/:user_id: 移除协作者或主动退出账本
 *          - POST   /api/ledgers/:id/invite-code: 生成或刷新 6 位加入邀请码
 *          - POST   /api/ledgers/join: 通过 6 位邀请码加入账本
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_ledger_routes(csilk_app_t* app);
