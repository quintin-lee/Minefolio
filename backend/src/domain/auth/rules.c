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
