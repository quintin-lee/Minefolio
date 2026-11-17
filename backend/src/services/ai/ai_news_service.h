#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将原始资产列表转化为脱敏持仓快照（仅保留名称、分类与百分比权重）
 *
 * 绝对不包含 current_value, cost_basis, quantity 等金额字段，防止资产规模泄露给 LLM。
 * 
 * @param[in] assets_json 包含原始持仓的 JSON 数组（各元素含 asset_name, asset_type/category, current_value 等）
 * @param[out] out_masked_json 脱敏后的 JSON 数组（元素包含 name, category, weight）
 * @param[out] out_total_value 可选的总市值输出
 * @return int 0 成功，-1 失败
 */
int ai_news_mask_portfolio(const csilk_json_t* assets_json,
                           csilk_json_t**      out_masked_json,
                           double*             out_total_value);

/**
 * @brief 获取或生成当天的全局大盘摘要（Stage 1，全局单次，零隐私）
 * @param pool DB 连接池
 * @param report_date 指定日期 ('YYYY-MM-DD')，若为 NULL 则使用当天
 * @param force_refresh 是否强制重新生成 (1 为强制)
 * @return market_news_digests 行对象 (含 id, digest_date, title, macro_summary, sector_impacts 等)；调用方负责 csilk_json_free
 */
csilk_json_t*
ai_news_get_or_create_digest(csilk_db_pool_t* pool, const char* report_date, int force_refresh);

/**
 * @brief 获取用户指定账本最近 N 天的 AI 资讯分析报告（轻量投影，不含 analysis/sources 大字段）
 * @param ledger_id 账本 ID (<=0 表示所有账本)
 * @param offset  分页偏移 (>=0)
 * @param out_has_more 若非 NULL, 输出是否还有下一页 (1/0)
 */
csilk_json_t* ai_news_list_ledger(csilk_db_pool_t* pool,
                                  int64_t          user_id,
                                  int64_t          ledger_id,
                                  int              limit,
                                  int              offset,
                                  int*             out_has_more);

/**
 * @brief 兼容旧版：获取用户最近 N 天的 AI 资讯分析报告
 */
csilk_json_t*
ai_news_list(csilk_db_pool_t* pool, int64_t user_id, int limit, int offset, int* out_has_more);

/**
 * @brief 获取用户指定账本指定日期的报告
 */
csilk_json_t* ai_news_get_by_date_ledger(csilk_db_pool_t* pool,
                                         int64_t          user_id,
                                         int64_t          ledger_id,
                                         const char*      report_date);

/**
 * @brief 兼容旧版：获取用户指定日期的报告
 */
csilk_json_t* ai_news_get_by_date(csilk_db_pool_t* pool, int64_t user_id, const char* report_date);

/**
 * @brief 触发指定账本的 AI 资讯分析（异步：创建 pending 记录，后台 worker 完成）
 * @param ledger_id 账本 ID (<=0 自动回退至用户默认账本)
 * @return 新记录 ID；-1 失败
 */
int64_t ai_news_trigger_ledger(csilk_db_pool_t* pool, int64_t user_id, int64_t ledger_id);

/**
 * @brief 兼容旧版：触发默认账本的 AI 资讯分析
 */
int64_t ai_news_trigger(csilk_db_pool_t* pool, int64_t user_id);

/**
 * @brief 调度器每日 tick：生成全局公共大盘摘要（Stage 1）
 */
void ai_news_scheduler_tick(csilk_db_pool_t* pool);

#ifdef __cplusplus
}
#endif
