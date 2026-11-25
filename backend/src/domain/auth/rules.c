#include "domain/auth/rules.h"
#include <stdio.h>
#include <string.h>

int
mf_auth_rule_validate_username(const char* username, char* err_buf, size_t err_len)
{
    if (!username || !username[0]) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "用户名不能为空");
        }
        return -1;
    }
    size_t len = strlen(username);
    if (len < 2) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "用户名长度需大于等于 2 字符");
        }
        return -1;
    }
    if (len > 64) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "用户名长度不能超过 64 字符");
        }
        return -1;
    }
    return 0;
}

int
mf_auth_rule_validate_password(const char* password, char* err_buf, size_t err_len)
{
    if (!password || !password[0]) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "密码不能为空");
        }
        return -1;
    }
    size_t len = strlen(password);
    if (len < 6) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "密码长度需大于等于 6 字符");
        }
        return -1;
    }
    if (len > 128) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "密码长度不能超过 128 字符");
        }
        return -1;
    }
    return 0;
}

bool
mf_auth_rule_can_register(int existing_user_count, bool allow_registration)
{
    if (existing_user_count <= 0) {
        /* 首个用户初始化引导，无论配置如何均必须允许注册 */
        return true;
    }
    return allow_registration;
}

bool
mf_auth_rule_verify_backup_code(char        backup_codes[16][16],
                                int         count,
                                const char* input_code,
                                int*        out_matched_index)
{
    if (!input_code || !input_code[0] || count <= 0) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (backup_codes[i][0] != '\0' && strcmp(backup_codes[i], input_code) == 0) {
            if (out_matched_index) {
                *out_matched_index = i;
            }
            return true;
        }
    }

    return false;
}

bool
mf_auth_rule_validate_user_id(int64_t user_id)
{
    return user_id > 0;
}

int
mf_auth_rule_validate_role(const char* role, char* err_buf, size_t err_len)
{
    if (!role || !role[0]) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "角色不能为空");
        }
        return -1;
    }
    if (strcmp(role, "admin") != 0 && strcmp(role, "user") != 0) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "无效的用户角色，仅支持 admin 或 user");
        }
        return -1;
    }
    return 0;
}

int
mf_auth_rule_can_update_user(int64_t     actor_id,
                             int64_t     target_id,
                             bool        target_is_active_admin,
                             const char* new_role,
                             bool        new_disabled,
                             int         active_admin_count,
                             char*       err_buf,
                             size_t      err_len)
{
    if (actor_id <= 0 || target_id <= 0) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "无效的用户 ID");
        }
        return -1;
    }

    if (new_role && mf_auth_rule_validate_role(new_role, err_buf, err_len) != 0) {
        return -1;
    }

    if (actor_id == target_id) {
        if (new_disabled) {
            if (err_buf && err_len) {
                snprintf(err_buf, err_len, "不能禁用当前登录的管理员账号");
            }
            return -1;
        }
        if (new_role && strcmp(new_role, "admin") != 0) {
            if (err_buf && err_len) {
                snprintf(err_buf, err_len, "不能取消当前登录账号的管理员权限");
            }
            return -1;
        }
    }

    bool removing_active_admin =
        target_is_active_admin && (new_disabled || (new_role && strcmp(new_role, "admin") != 0));
    if (removing_active_admin && active_admin_count <= 1) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "系统至少需要保留一名可用的管理员账号");
        }
        return -1;
    }

    return 0;
}

int
mf_auth_rule_can_delete_user(int64_t actor_id,
                             int64_t target_id,
                             bool    target_is_active_admin,
                             int     active_admin_count,
                             char*   err_buf,
                             size_t  err_len)
{
    if (actor_id <= 0 || target_id <= 0) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "无效的用户 ID");
        }
        return -1;
    }

    if (actor_id == target_id) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "不能删除当前登录的管理员账号");
        }
        return -1;
    }

    if (target_is_active_admin && active_admin_count <= 1) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "系统至少需要保留一名可用的管理员账号");
        }
        return -1;
    }

    return 0;
}
