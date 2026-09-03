#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/financial/currency.h"
#include "core/financial/price.h"
#include "core/financial/money.h"
#include "core/financial/quantity.h"

/**
 * @brief 资产行情报价实体 (Market Quote Entity)
 * @note 严格禁止依赖任何外部 DB、传输协议或 JSON 框架
 */
typedef struct mf_market_quote {
    char       symbol[64];        /**< 标的代码 (如 "sh600519", "AAPL", "BTC") */
    char       name[128];         /**< 标的名称/公司简称 */
    char       source[32];        /**< 行情数据来源 (如 "stock_cn", "fund_cn", "yahoo", "binance") */
    price_t    current_price;     /**< 最新成交价/单位净值 */
    double     change_percent;    /**< 当日涨跌幅百分比 (如 2.35 表示 +2.35%) */
    currency_t currency;          /**< 标的计价货币 */
    char       quote_time[32];    /**< 报价时间戳 */
} mf_market_quote_t;

/**
 * @brief 证券代码搜索候选项实体 (Market Search Item Entity)
 */
typedef struct mf_market_search_item {
    char       symbol[64];
    char       name[128];
    char       source[32];
    char       market_desc[64];
    price_t    current_price;
    currency_t currency;
} mf_market_search_item_t;

/**
 * @brief 市场行情同步系统配置实体 (Market Settings Entity)
 */
typedef struct mf_market_settings {
    char market_proxy[256];        /**< HTTP/HTTPS/SOCKS5 网络代理地址 */
    bool market_auto_sync;         /**< 是否启用后台行情自动同步定时任务 */
    int  market_sync_interval_min; /**< 自动同步轮询间隔时间 (分钟) */
    char market_sync_mode[32];     /**< 同步模式策略: "trading_hours", "all_day" */
} mf_market_settings_t;

/**
 * @brief 汇率实体 (Exchange Rate Entity)
 */
typedef struct mf_exchange_rate {
    char   base_currency[16];   /**< 基准外币 (如 "USD", "EUR", "HKD") */
    char   target_currency[16]; /**< 目标货币 (默认 "CNY") */
    double rate;                /**< 折算汇率 */
    char   updated_at[64];
} mf_exchange_rate_t;
