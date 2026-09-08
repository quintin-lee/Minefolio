#pragma once

/**
 * @file rules.h
 * @brief 分类领域业务规则 (Domain Category Rules)
 */

#include <stdbool.h>
#include <stdint.h>

/** 合法的分类主类别集合 */
#define MF_CATEGORY_VALID_TYPES "expense,income,asset,transaction"

/**
 * @brief 校验分类名称合法性
 */
bool mf_category_rule_validate_name(const char* name);

/**
 * @brief 校验分类类型合法性
 */
bool mf_category_rule_validate_type(const char* type);

/**
 * @brief 判断是否可以删除该分类（无子分类时可删）
 */
bool mf_category_rule_can_delete(int64_t child_count);
