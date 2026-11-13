/**
 * @file commands.h
 * @brief 报表域命令/查询参数与结果 DTO (Application Layer)
 *
 * 将 HTTP 上下文解耦：controller 负责从 csilk_ctx_t 解析 user/ledger/query
 * 参数并填充 report_query_t；usecase 与 service 层仅消费纯数据结构，
 * 不再依赖 csilk_ctx_t / ctx_* / respond_* 等 HTTP 框架类型。
 *
 * 结果统一封装：report_result_t 携带 code/message（与全局响应信封一致），
 * data_payload 为 usecase 层构造好的 csilk_json_t*（非空时由响应层挂载并释放）。
 */

#pragma once

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 报表查询参数聚合
 *
 * - user_id / ledger_id：由 controller 通过 ctx_user_id / ctx_ledger_id 解析（>0 有效；
 *   ledger_id 可为 0 表示使用默认账本）。
 * - 各可选 query 参数（year/month/days/period/base_currency）若客户端未提供则为 NULL，
 *   service 层按默认值处理。
 */
typedef struct {
    int64_t     user_id;
    int64_t     ledger_id;
    const char* year;  /**< 可选 "YYYY" */
    const char* month; /**< 可选 "MM" */
    int         days; /**< 趋势/走势采样天数（<=0 表示未指定，service 取默认） */
    const char* period;        /**< 可选趋势周期字符串，如 "30d"/"90d"/"365d"/"1y" */
    const char* base_currency; /**< 可选基准币种，默认 "CNY" */
} report_query_t;

/**
 * @brief 报表用例统一结果（与全局响应信封 code 语义一致）
 *
 * - code == 0：成功，data_payload 为响应 JSON（调用方负责挂载/释放）；
 * - code != 0：失败，data_payload 为 NULL，message 描述原因。
 */
typedef struct {
    int           code;
    char          message[256];
    csilk_json_t* data_payload; /**< code==0 时的响应对象（所有权转交响应层） */
} report_result_t;

#ifdef __cplusplus
}
#endif
