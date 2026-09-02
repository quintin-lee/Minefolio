#pragma once
#include "csilk/csilk.h"
#include "core/financial/currency.h"
#include "core/financial/decimal.h"
#include "core/financial/money.h"
#include "core/financial/rate.h"

/**
 * @file exchange_rate_service.h
 * @brief 实时多币种外汇汇率引擎与历史汇率快照管理服务（Financial Core 驱动）
 *
 * 提供主流法币及数字货币对基准币种 (CNY) 的汇率查询、任意双币种实时折算、
 * Yahoo Finance 外汇行情拉取、每日历史汇率快照落地与走势查询功能。
 */

/**
 * @brief 查询源币种到目标币种的高精度汇率对象
 */
rate_t exchange_rate_get_rate(currency_t from_cur, currency_t to_cur);

/**
 * @brief 使用高精度定点数引擎在任意双币种之间进行金额换算
 */
money_t exchange_rate_convert_m(money_t amount, currency_t to_currency);

/**
 * @brief 查询指定币种对基准币种 (CNY) 的实时折算汇率。
 *
 * @param currency 币种 ISO 3位代码（如 "USD", "EUR", "HKD", "JPY" 等，不区分大小写）
 * @return double  对应 1 单位目标外币折合 CNY 的汇率值（如 7.20）；若为 CNY 则返回 1.0；未知币种返回 1.0 兜底。
 */
double exchange_rate_get_to_cny(const char* currency);

/**
 * @brief 在任意两个币种之间进行金额换算。
 *
 * 换算原理：通过基准币种 CNY 作为中介桥梁实现三角套汇转换：
 * Amount_to = Amount_from * (Rate_from_to_CNY / Rate_to_to_CNY)
 *
 * @param amount        原始金额
 * @param from_currency 源币种代码（如 "USD"）
 * @param to_currency   目标币种代码（如 "HKD"）
 * @return double       折算后的目标币种金额
 */
double exchange_rate_convert(double amount, const char* from_currency, const char* to_currency);

/**
 * @brief 手动设置或覆盖某一币种对 CNY 的基准汇率，并自动记录历史快照。
 *
 * @param currency    目标币种代码（如 "USD"）
 * @param rate_to_cny 对 CNY 汇率数值（必须大于 0）
 * @return int        0 成功更新并同步历史表；-1 参数非法或更新失败。
 */
int exchange_rate_set(const char* currency, double rate_to_cny);

/**
 * @brief 从 Yahoo Finance 等远端外汇源拉取刷新所有主流币种的实时外汇牌价。
 *
 * 具备多线程互斥安全锁保护，抓取成功后自动触发 exchange_rate_history 的当日快照更新。
 */
void exchange_rate_refresh_all(void);

/**
 * @brief 获取当前系统中所有已注册币种的最新汇率字典。
 *
 * @return csilk_json_t* 格式如 {"CNY": 1.0, "USD": 7.23, "EUR": 7.85, ...} 的 JSON 对象（调用方负责释放）。
 */
csilk_json_t* exchange_rate_list_all(void);

/**
 * @brief 查询指定币种在最近 N 天内的每日汇率走势历史记录。
 *
 * @param target_currency 目标外币代码（如 "USD"）
 * @param days            查询历史天数跨度（默认为 30 天）
 * @return csilk_json_t*  按日期升序排列的历史点数组 [ { "rate_date": "2026-08-01", "rate": 7.20 }, ... ]（调用方负责释放）。
 */
csilk_json_t* exchange_rate_history_list(const char* target_currency, int days);
