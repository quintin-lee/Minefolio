# Minefolio 自动行情与净值同步系统设计文档

## 1. 概述与背景

Minefolio 是一款自托管个人财务与投资资产管理系统。在此前版本中，用户的投资标的（公募基金、股票、加密货币、贵金属等）的单位净值（`net_value`）依赖手动维护，无法自动跟踪最新市场行情、每日涨跌幅与最新市值变动。

本项目旨在为 Minefolio 构建**全品类自动行情与净值同步子系统**，实现以下核心目标：
1. **智能代码联想与识别**：支持公募基金、A股、港美股、加密货币、贵金属的即时搜索与信息自动填充。
2. **多驱动聚合引擎**：基于轻量免 Key 公共源（天天基金、腾讯行情、Binance/CoinGecko 等）实现高可靠、多品类行情抓取，并支持全局 HTTP 代理。
3. **持仓净值联动与历史沉淀**：自动更新标的最新净值与持仓市值，记录每日收盘价到历史价格表（`asset_price_history`），为净值走势图及后续收益率（XIRR/TWRR）计算打下数据基础。
4. **灵活同步策略**：支持前端「一键全量刷新」、「单标的刷新」及后端「交易时段与夜间清算定时调度」。

---

## 2. 总体架构与数据流

### 2.1 架构设计

```
[ Frontend: Vue 3 + TypeScript ]
  ├── 资产录入/编辑 (智能搜索下拉联想 / 自动填入 symbol & 净值)
  ├── 持仓列表 (一键同步按钮 / 今日涨跌幅 / 上次同步时间)
  ├── 净值走势图 (ECharts 历史价格曲线)
  └── 系统设置 (HTTP 代理配置 / 自动同步频率 / 行情源测试)
           │ HTTP (RESTful JSON)
           ▼
[ Backend: C23 (csilk) ]
  ├── Controllers: market_controller.c (Search, Sync, History, Settings)
  ├── Services: market_service.c (Orchestration, Batch Sync, Asset Linkage)
  ├── Drivers: quote_engine.c
  │     ├── driver_eastmoney.c (公募基金搜索与净值)
  │     ├── driver_tencent.c   (A股 / 港股 / 美股 / 黄金批量行情)
  │     └── driver_crypto.c    (加密货币 BTC / ETH / USDT 计价)
  ├── Scheduler: market_scheduler.c (交易时段轮询 + 22:00 基金清算定时调度)
  └── Repositories:
        ├── asset_repo.c (更新 net_value, current_value, last_sync_at)
        ├── price_history_repo.c (新增 asset_price_history 表记录)
        └── market_settings_repo.c (存储代理与定时配置)
```

### 2.2 核心数据流

#### 1. 标的搜索流程 (Search Flow)
```
用户输入关键词 ("110011" 或 "茅台" 或 "AAPL" 或 "BTC")
  -> GET /api/market/search?keyword=...
  -> market_service 调度各注册驱动执行 search()
  -> 聚合匹配结果并去重评分
  -> 返回 [{ symbol, name, source, market_desc, current_price, currency }]
  -> 前端下拉展示并支持一键带入表单
```

#### 2. 行情同步流程 (Sync Flow)
```
用户点击「一键同步」或后台定时器触发
  -> POST /api/market/sync
  -> 从 assets 查询所有已配置 symbol != '' 的资产
  -> 按 quote_source 分组 (如 fund_cn, stock_cn, stock_us, crypto)
  -> 分发至对应驱动批量抓取最新价格 (如腾讯接口批量拉取 q=sh600519,sz000001,usAAPL)
  -> 事务更新:
       1) assets.net_value = quote.price
       2) if assets.quantity > 0: assets.current_value = assets.quantity * quote.price
       3) assets.last_sync_at = NOW()
       4) INSERT OR REPLACE INTO asset_price_history(asset_id, price_date, price, currency)
  -> 返回同步结果统计 { total, updated, failed, items: [...] }
```

---

## 3. 数据库模型变更

### 3.1 扩展现有 `assets` 表
新增标的代码、驱动标识与同步时间戳字段：

```sql
-- SQLite & PostgreSQL
ALTER TABLE assets ADD COLUMN symbol TEXT DEFAULT '';
ALTER TABLE assets ADD COLUMN quote_source TEXT DEFAULT '';
ALTER TABLE assets ADD COLUMN last_sync_at TIMESTAMP;
```

### 3.2 新增 `asset_price_history` (历史净值/价格表)
用于记录每个资产每日的收盘价或单位净值：

```sql
-- SQLite
CREATE TABLE IF NOT EXISTS asset_price_history (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id    INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    price_date  DATE NOT NULL,
    price       DECIMAL(18,4) NOT NULL,
    currency    TEXT DEFAULT 'CNY',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(asset_id, price_date)
);

CREATE INDEX IF NOT EXISTS idx_price_history_asset_date 
    ON asset_price_history(asset_id, price_date DESC);
```

```sql
-- PostgreSQL
CREATE TABLE IF NOT EXISTS asset_price_history (
    id          BIGSERIAL PRIMARY KEY,
    asset_id    BIGINT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    price_date  DATE NOT NULL,
    price       DOUBLE PRECISION NOT NULL,
    currency    VARCHAR(16) DEFAULT 'CNY',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(asset_id, price_date)
);

CREATE INDEX IF NOT EXISTS idx_price_history_asset_date 
    ON asset_price_history(asset_id, price_date DESC);
```

### 3.3 新增/复用系统配置 (`market_settings`)
持久化在 `ai_settings` 表或专用的 `market_settings` 表中存储配置 JSON：
```json
{
  "market_proxy": "",
  "market_auto_sync": true,
  "market_sync_interval_min": 15
}
```

---

## 4. 后端核心模块设计

### 4.1 统一数据结构 (`backend/src/common/market_types.h`)
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char   symbol[64];         /* 标的代码，如 110011, sh600519, AAPL, BTCUSDT */
    char   name[128];          /* 标的名称，如 易方达中小盘, 贵州茅台, 苹果, 比特币 */
    char   source[32];         /* 驱动标识: fund_cn, stock_cn, stock_us, crypto, metal */
    double current_price;      /* 最新单位净值 / 当前市价 */
    double change_percent;     /* 当日涨跌幅百分比 (如 1.52 表示 +1.52%) */
    char   currency[16];       /* 货币单位，如 CNY, USD */
    char   quote_time[32];     /* 行情时间 / 净值日期，如 2026-08-28 15:00:00 */
} market_quote_t;

typedef struct {
    char symbol[64];
    char name[128];
    char source[32];
    char market_desc[64];      /* 描述，如 "A股主板", "开放式基金", "美股纳斯达克" */
} market_search_item_t;
```

### 4.2 驱动接口定义 (`backend/src/services/market/quote_driver.h`)
```c
#pragma once
#include "common/market_types.h"

typedef struct quote_driver_s quote_driver_t;

struct quote_driver_s {
    const char* name;          /* 驱动名称: "eastmoney", "tencent", "crypto" */
    const char* source_type;   /* 支持类型: "fund_cn", "stock_cn", "stock_us", "crypto", "metal" */
    
    /* 搜索标的 */
    int (*search)(const char* keyword, market_search_item_t* out_items, int max_items);
    
    /* 单标的拉取 */
    int (*fetch_single)(const char* symbol, market_quote_t* out_quote);
    
    /* 批量拉取 (若不支持则逐个调用 fetch_single) */
    int (*fetch_batch)(const char** symbols, int count, market_quote_t* out_quotes, int* out_count);
};
```

### 4.3 驱动实现细节
1. **天天基金驱动 (`driver_eastmoney.c`)**：
   - 搜索：调用东方财富建议搜索接口获取基金代码与名称。
   - 净值：调用 `http://fundgz.1234567.com.cn/js/{code}.js` 或手机端 API 获取 `dwjz` (单位净值)、`gsz` (估算净值)、`jzrq` (净值日期) 与 `gszzl` (日涨跌幅)。
2. **腾讯行情驱动 (`driver_tencent.c`)**：
   - 接口：`http://qt.gtimg.cn/q={symbols}`
   - 格式映射：
     - A股：`sh600519`, `sz000001`, `bj830946`
     - 港股：`hk00700`
     - 美股：`usAAPL`
     - 黄金：`s_au9999`
   - 特点：单次 HTTP 请求可同时携带数十个标的，支持超高吞吐与毫秒级延迟解析。
3. **加密货币驱动 (`driver_crypto.c`)**：
   - 接口：Binance Ticker API (`https://api.binance.com/api/v3/ticker/24hr?symbol={symbol}`) 或 CoinGecko API，解析最新成交价与 24h 涨跌幅。

### 4.4 调度器与容错设计 (`market_scheduler.c`)
- 使用后台工作线程运行基于时间戳的事件循环。
- 网络请求使用 `libcurl`，支持配置 `CURLOPT_TIMEOUT` (5秒) 及 `CURLOPT_PROXY`。
- 防击穿与脏数据保护：当接口超时或异常时，不覆盖现有数据库中的有效数据，保留最近有效净值。

---

## 5. API 接口定义

| 方法 | 路径 | 认证 | 说明 |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/market/search` | JWT | 标的模糊搜索，支持参数 `keyword` |
| `POST` | `/api/market/sync` | JWT | 全量或指定资产批量同步最新行情与净值 |
| `POST` | `/api/market/sync/:asset_id` | JWT | 单个资产同步行情 |
| `GET` | `/api/market/history/:asset_id` | JWT | 获取资产的历史净值走势（支持 `limit` 参数） |
| `GET` | `/api/market/settings` | JWT | 获取行情同步与代理配置 |
| `PUT` | `/api/market/settings` | JWT | 更新行情同步与代理配置 |
| `POST` | `/api/market/settings/test` | JWT | 测试行情源网络连通性 |

---

## 6. 前端模块设计

### 6.1 标的搜索与自动填充 (`AssetFormDialog.vue` / `Holdings.vue`)
- 在资产类型属于投资品时，显示标的智能搜索框。
- 使用 `el-autocomplete` 异步拉取 `/api/market/search`，选定后自动写入 `symbol`, `quote_source`, `name`, `currency`, `net_value`。

### 6.2 持仓管理一键同步 (`Holdings.vue`)
- 页头放置 `🔄 同步最新行情` 按钮，绑定 `POST /api/market/sync`。
- 表格列展示最新净值、当日涨跌幅徽章及上次同步时间提示。
- 每行支持单独点击刷新按钮。

### 6.3 历史净值走势图组件 (`AssetPriceChart.vue`)
- 基于 ECharts 封装轻量折线图，展示近 30/90/365 天的历史净值与收益走势。

### 6.4 系统设置 (`Settings.vue`)
- 增加「行情同步设置」面板：配置 HTTP 代理、自动同步开关、轮询间隔与一键连通性测试。

---

## 7. 实施与验证步骤

1. **数据库迁移**：更新 `migration.sql` 与 `migration_postgres.sql`，执行字段升级。
2. **后端驱动与引擎开发**：
   - 实现 `market_types.h`、`quote_driver.h`、`quote_engine.c`
   - 实现 `driver_eastmoney.c`、`driver_tencent.c`、`driver_crypto.c`
   - 实现 `price_history_repo.c` 与 `market_service.c`、`market_controller.c`
   - 实现后台定时调度器 `market_scheduler.c`
3. **前端交互与组件开发**：
   - 增加 `frontend/src/api/market.ts`
   - 改造 `Holdings.vue`、`Assets.vue`，加入智能搜索与一键同步
   - 新增 `AssetPriceChart.vue` 与 `Settings.vue` 行情配置项
4. **端到端集成测试**：
   - 模拟搜索基金、A股、美股、加密货币
   - 验证一键同步与资产净值/市值自动重新核算
   - 验证历史价格表写入与走势图渲染
   - 验证无网络/代理故障时的降级保护
