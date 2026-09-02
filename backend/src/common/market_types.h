#pragma once

/**
 * @file market_types.h
 * @brief 市场行情与证券搜索数据模型定义
 *
 * 定义证券资产的实时/历史行情报价（market_quote_t）、
 * 代码搜索结果条目（market_search_item_t）以及市场数据源全局同步配置（market_settings_t）。
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * @struct market_quote_t
 * @brief 资产行情报价数据模型
 */
typedef struct {
    char   symbol[64];     /**< 标的代码（如 "AAPL", "00700.HK", "600519.SH"） */
    char   name[128];      /**< 标的名称/公司简称 */
    char   source[32];     /**< 行情数据来源通道（如 "sina", "tencent", "alphavantage" 等） */
    double current_price;  /**< 最新成交价 */
    double change_percent; /**< 当日涨跌幅百分比（如 2.35 表示 +2.35%） */
    char   currency[16];   /**< 标的计价货币代码（如 "CNY", "USD", "HKD"） */
    char   quote_time[32]; /**< 行情报价产生或更新时间戳 */
} market_quote_t;

/**
 * @struct market_search_item_t
 * @brief 证券代码搜索候选项模型
 */
typedef struct {
    char   symbol[64];      /**< 标的代码 */
    char   name[128];       /**< 标的名称 */
    char   source[32];      /**< 数据源 */
    char   market_desc[64]; /**< 市场板块或分类描述（如 "A股", "港股", "美股"） */
    double current_price;   /**< 搜索时刻的最新价格 */
    char   currency[16];    /**< 计价货币 */
} market_search_item_t;

/**
 * @struct market_settings_t
 * @brief 市场行情同步与网络代理系统配置模型
 */
typedef struct {
    char market_proxy[256];        /**< HTTP/HTTPS/SOCKS5 网络代理地址（用于访问境外行情源） */
    bool market_auto_sync;         /**< 是否启用后台行情自动同步定时任务 */
    int  market_sync_interval_min; /**< 自动同步轮询间隔时间（分钟） */
    char market_sync_mode[32];     /**< 同步模式策略（如 "realtime", "daily_close"） */
} market_settings_t;
