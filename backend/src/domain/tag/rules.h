#pragma once

/**
 * @file rules.h
 * @brief 标签领域业务规则 (Domain Tag Rules)
 *
 * 纯业务校验函数，零外部依赖。
 */

#include <stdbool.h>

/**
 * @brief 校验标签名称合法性
 * @param name 标签名称字符串
 * @return true: 合法, false: 非法
 */
bool mf_tag_rule_validate_name(const char* name);

/**
 * @brief 校验标签颜色格式合法性
 * @param color 颜色字符串（可选，NULL 或空串视为合法——使用默认色）
 * @return true: 合法, false: 非法
 */
bool mf_tag_rule_validate_color(const char* color);
