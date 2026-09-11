#pragma once
#include "common/market_types.h"
#include <stddef.h>

/**
 * @file driver_mock.h
 * @brief 离线行情 mock 驱动
 *
 * 当环境变量 MINEFOLIO_MARKET_MOCK 被设置为真值（非 "0"/"false"）时，
 * 行情引擎（quote_engine）用固定的确定性报价替代真实 HTTP 抓取，
 * 使行情相关集成测试可完全离线、稳定地运行（不依赖 Yahoo/东财等外部行情源）。
 *
 * 生产环境不设该变量，行为不变，始终走真实行情驱动。
 */

/** @return 1 若启用了离线 mock，否则 0。 */
int market_mock_enabled(void);

/**
 * 固定的证券搜索：针对已知测试标的返回候选项，其余返回占位项。
 * @return 写入 out_items 的条目数量（<= max_items）。
 */
int market_mock_search(const char* keyword, market_search_item_t* out_items, int max_items);

/**
 * 固定的单标报价：对已知测试标的返回确定性价格，未知标的返回占位价。
 * @return 0 成功；-1 参数非法。
 */
int market_mock_fetch_single(const char* symbol, market_quote_t* out_quote);

/**
 * 固定的连通性探测：恒返回成功（离线 mock 无需真实网络）。
 * @return 0 成功。
 */
int market_mock_test_connection(char* out_msg, size_t msg_cap, int* out_latency_ms);
