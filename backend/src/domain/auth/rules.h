#pragma once

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 校验用户名长度及合法性
 */
int mf_auth_rule_validate_username(const char* username, char* err_buf, size_t err_len);

/**
 * @brief 校验明文密码长度及安全要求 (>= 6 字符)
 */
int mf_auth_rule_validate_password(const char* password, char* err_buf, size_t err_len);

/**
 * @brief 判断当前系统是否允许开放用户注册
 * @param existing_user_count 当前已存在用户数 (0 表示系统初始化)
 * @param allow_registration 系统配置是否开放后续注册
 */
bool mf_auth_rule_can_register(int existing_user_count, bool allow_registration);

/**
 * @brief 校验 TOTP 应急备用恢复码是否匹配有效
 */
bool mf_auth_rule_verify_backup_code(char backup_codes[16][16], int count,
                                    const char* input_code, int* out_matched_index);
