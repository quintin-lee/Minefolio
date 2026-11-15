#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取用户最近 N 天的 AI 资讯分析报告
 */
csilk_json_t* ai_news_list(csilk_db_pool_t* pool, int64_t user_id, int limit);

/**
 * @brief 获取用户指定日期的报告
 */
csilk_json_t* ai_news_get_by_date(csilk_db_pool_t* pool, int64_t user_id, const char* report_date);

/**
 * @brief 触发一次 AI 资讯分析（异步：创建 pending 记录，后台 worker 完成）
 * @return 新记录 ID；-1 失败
 */
int64_t ai_news_trigger(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 调度器每日 tick：为所有活跃用户生成当日报告
 */
void ai_news_scheduler_tick(csilk_db_pool_t* pool);

#ifdef __cplusplus
}
#endif
