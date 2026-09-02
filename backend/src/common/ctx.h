#pragma once

/**
 * @file ctx.h
 * @brief 请求上下文辅助工具与权限校验接口
 *
 * 提供基于 HTTP 请求上下文快速提取已认证用户 ID、生成 SQL 参数字符串、
 * 以及获取并验证账本空间 ID (ledger_id) 和用户角色的内联工具函数。
 */

#include "csilk/csilk.h"
#include "common/jwt.h"
#include "common/response.h"
#include "repositories/ledger_repo.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 从请求上下文中提取已认证的用户 ID
 *
 * 从 HTTP 请求上下文中检索经过 JWT 中间件校验的用户 ID。
 * 若提取失败（未登录或 Token 无效），将自动向客户端发送 401 Unauthorized 响应。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 *
 * @return int64_t 用户 ID
 * @retval >0 成功提取的用户主键 ID
 * @retval -1 提取失败（未通过认证），已发送 401 响应
 *
 * @note 线程安全性：线程安全，仅读取当前请求上下文。
 */
static inline int64_t
ctx_user_id(csilk_ctx_t* c)
{
    int64_t uid = jwt_get_user_id(c);
    if (uid < 0) {
        respond_unauthorized(c);
        return -1;
    }
    return uid;
}

/**
 * @brief 将 int64_t 用户 ID 格式化为固定长度字符串
 *
 * 用于方便地构建 SQL 参数化查询数组。
 *
 * @param[in] user_id 目标用户 ID
 * @param[out] out 接收格式化字符串的字符数组（缓冲区长度至少为 32 字节）
 *
 * @note 内存所有权：由调用方提供缓冲区。
 */
static inline void
ctx_uid_str(int64_t user_id, char out[static 32])
{
    snprintf(out, 32, "%lld", (long long)user_id);
}

/**
 * @brief 提取并校验当前上下文关联的账本 ID (ledger_id) 与权限角色
 *
 * 执行步骤：
 * 1. 优先从 HTTP 请求头 "X-Ledger-Id" 或 URL Query 参数 "ledger_id" 中解析账本 ID。
 * 2. 若未指定，则回退查询用户的默认个人账本。
 * 3. 校验用户是否属于该账本成员，并检查是否满足指定的角色权限要求（如 'owner', 'editor', 'viewer'）。
 * 4. 权限不足或账本不存在时，自动返回 1004 Forbidden 错误响应并返回 -1。
 *
 * @param[in,out] c csilk HTTP 上下文指针，不可为 NULL
 * @param[in] user_id 当前发起请求的用户 ID
 * @param[in] required_role 所需的最低权限角色字符串（可为 NULL、"owner" 或 "editor"）
 *
 * @return int64_t 校验通过的账本 ID
 * @retval >0 合法且满足角色要求的 ledger_id
 * @retval -1 账本不存在、用户非成员或权限不足（已向客户端发送 1004 响应）
 *
 * @note 线程安全性：线程安全，内部通过连接池查询数据库。
 */
static inline int64_t
ctx_ledger_id(csilk_ctx_t* c, int64_t user_id, const char* required_role)
{
    if (user_id <= 0) {
        return -1;
    }

    const char* lid_header = csilk_get_header(c, "X-Ledger-Id");
    const char* lid_query = csilk_get_query(c, "ledger_id");
    int64_t     lid = 0;
    if (lid_header && lid_header[0]) {
        lid = atoll(lid_header);
    } else if (lid_query && lid_query[0]) {
        lid = atoll(lid_query);
    }

    csilk_db_pool_t* pool = db_get_pool();
    if (lid <= 0) {
        lid = ledger_get_default(pool, user_id);
    }
    if (lid <= 0) {
        respond_error(c, 1004, "No active ledger found");
        return -1;
    }

    if (required_role && required_role[0]) {
        char role[32] = {0};
        if (!ledger_get_user_role(pool, lid, user_id, role, sizeof(role))) {
            respond_error(c, 1004, "Forbidden: you are not a member of this ledger");
            return -1;
        }

        if (strcmp(required_role, "owner") == 0) {
            if (strcmp(role, "owner") != 0) {
                respond_error(c, 1004, "Forbidden: owner permission required");
                return -1;
            }
        } else if (strcmp(required_role, "editor") == 0) {
            if (strcmp(role, "owner") != 0 && strcmp(role, "editor") != 0) {
                respond_error(c, 1004, "Forbidden: editor permission required (read-only mode)");
                return -1;
            }
        }
    }

    return lid;
}
