#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "domain/cashflow/entity.h"

/**
 * @brief 校验现金流计划基本信息完整性与合法性
 */
int mf_cashflow_rule_validate(const mf_cashflow_schedule_t* s, char* err_buf, size_t err_len);

/**
 * @brief 根据发生频率获取年化计算系数
 */
int mf_cashflow_rule_annual_factor(const char* freq, double* out_factor);

/**
 * @brief 判断周期性排程是否在指定的年、月发生，若发生则计算落点具体天数
 */
bool mf_cashflow_rule_matches_month(const char* freq,
                                    const char* start_date,
                                    const char* end_date,
                                    int         year,
                                    int         month,
                                    int*        out_day);
