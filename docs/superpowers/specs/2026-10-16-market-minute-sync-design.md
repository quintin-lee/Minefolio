# 大盘指数与外汇汇率分钟级同步与开盘期自适应设计规范

**文档标识**: `2026-10-16-market-minute-sync-design`  
**作者**: DeepMind Antigravity Pair Programmer & quintin  
**状态**: Approved (待实施)  
**创建日期**: 2026-10-16  

---

## 1. 概述与核心目标 (Executive Summary)

### 1.1 背景与痛点
1. **同步频次过于激进/低效**：
   - 原前端大盘看板轮询间隔为 15s / 30s / 60s（默认 30s），后端大盘缓存 TTL 仅 20s；
   - 无论全球股市是否处于闭市状态（如半夜或周末），系统仍盲目向外部上游（腾讯、Yahoo 等）请求行情，极易触发上游反爬限流与资源浪费。
2. **读写未严格分离，真理之源模糊**：
   - 大盘数据此前仅存储在后端的 C 内存静态结构体 `g_indices_cache` 中，未持久化入库；
   - 用户明确提出核心设计约束：**“前端展示的数据只从数据库中获取，同步的数据直接写入数据库”**。

### 1.2 核心目标
1. **严格 CQRS 读写分离（Database as Single Source of Truth）**：
   - **读取侧**：前端所有查询接口（`GET /api/market/indices`、`GET /api/market/exchange-rates`）**100% 只读数据库**，响应时间 < 5ms，0 外部网络 IO 依赖。
   - **写入侧**：后台定时调度器与手动触发同步任务时，从外部抓取行情后**直接 Upsert 写入数据库**。
2. **分钟级与开盘期自适应节拍**：
   - 大盘与外汇同步降频至分钟级（默认 1 分钟 / 3 分钟 / 5 分钟）；
   - **闭市标的跳过**：已闭市市场标的直接复用已有数据，跳过外部网络抓取；
   - **全休市智能休眠**：前端在各股市均处于休市（如周末或晚间）时，自动进入 15 分钟长周期休眠，开市时自动唤醒。

---

## 2. 总体架构与数据流 (Architecture & Data Flow)

```
       [ 外部行情源 (Tencent / Frankfurter / Yahoo) ]
                          │
                   (后台抓取 / 手动同步)
                          ▼
             [ 后端调度与同步引擎 (Write Path) ]
                          │
                 (SQL Upsert 直接入库)
                          ▼
           ================================
           数据库 (SQLite / PostgreSQL)
             - market_indices (大盘指数表)
             - exchange_rates (外汇汇率表)
             - exchange_rate_history (汇率历史表)
             - assets / asset_price_history (持仓行情)
           ================================
                          ▲
                 (SQL SELECT 纯数据库查询)
                          │
             [ REST API 接口层 (Read Path) ]
               - GET /api/market/indices
               - GET /api/market/exchange-rates
                          ▲
                 (HTTP 轮询 1m/3m/5m)
                          │
           [ 前端 Vue 3 看板 (Frontend Views) ]
```

---

## 3. 数据库持久化层变更 (Database Schema V013)

### 3.1 新建 `market_indices` 数据表
新增版本化迁移脚本 `V013__market_indices_table.sql`（分别写入 `backend/sql/migrations/sqlite/` 与 `backend/sql/migrations/postgres/`）：

#### PostgreSQL 结构：
```sql
CREATE TABLE IF NOT EXISTS market_indices (
    symbol          VARCHAR(32) PRIMARY KEY,
    name            VARCHAR(64) NOT NULL,
    category        VARCHAR(32) NOT NULL,         -- cn / hk / us / global / crypto
    region_flag     VARCHAR(16) NOT NULL,
    current_price   DOUBLE PRECISION NOT NULL DEFAULT 0,
    change_amount   DOUBLE PRECISION NOT NULL DEFAULT 0,
    change_percent  DOUBLE PRECISION NOT NULL DEFAULT 0,
    prev_close      DOUBLE PRECISION NOT NULL DEFAULT 0,
    open_price      DOUBLE PRECISION NOT NULL DEFAULT 0,
    high_price      DOUBLE PRECISION NOT NULL DEFAULT 0,
    low_price       DOUBLE PRECISION NOT NULL DEFAULT 0,
    currency        VARCHAR(16) NOT NULL DEFAULT 'CNY',
    quote_time      VARCHAR(32) NOT NULL DEFAULT '',
    sparkline       TEXT NOT NULL DEFAULT '[]',   -- JSON 数组字符串存储
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_market_indices_cat ON market_indices(category);
```

#### SQLite 结构：
```sql
CREATE TABLE IF NOT EXISTS market_indices (
    symbol          TEXT PRIMARY KEY,
    name            TEXT NOT NULL,
    category        TEXT NOT NULL,
    region_flag     TEXT NOT NULL,
    current_price   REAL NOT NULL DEFAULT 0,
    change_amount   REAL NOT NULL DEFAULT 0,
    change_percent  REAL NOT NULL DEFAULT 0,
    prev_close      REAL NOT NULL DEFAULT 0,
    open_price      REAL NOT NULL DEFAULT 0,
    high_price      REAL NOT NULL DEFAULT 0,
    low_price       REAL NOT NULL DEFAULT 0,
    currency        TEXT NOT NULL DEFAULT 'CNY',
    quote_time      TEXT NOT NULL DEFAULT '',
    sparkline       TEXT NOT NULL DEFAULT '[]',
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_market_indices_cat ON market_indices(category);
```

### 3.2 预置基线数据（Seed Data）
在 `V013` 脚本中插入 15 个主要全球标的的初始基线行（上证指数、深证成指、创业板指、沪深300、恒生指数、恒生科技、标普500、纳斯达克、道琼斯、日经225、德国DAX、英国富时、比特币、以太坊、黄金现货），保证数据库初始化即有可用基础大盘数据。

---

## 4. 后端写入侧：同步与直接入库 (Write Path)

### 4.1 大盘指数写入：`quote_engine_sync_indices_to_db`
- **执行入口**：`backend/src/services/market/quote_engine.c` 提供 `int quote_engine_sync_indices_to_db(csilk_db_pool_t* pool)`。
- **开闭市状态过滤**：
  1. 计算当前 5 大市场时钟状态（`SSE_SZSE`, `HKEX`, `TSE`, `LSE`, `NYSE`）；
  2. 针对每个预置大盘标的：
     - 若所属市场时钟状态为 `closed`，且数据库中已有非 0 报价记录：**直接跳过外部网络抓取**，保留上次收盘数据；
     - 若所属市场状态为 `open` 或 `pre`，或者为 7x24 加密货币（`crypto`）：发起单标的外部轻量网络抓取；
  3. 计算开高低收和 8 点 Sparkline 趋势，执行 SQL Upsert：
     ```sql
     -- SQLite Upsert
     INSERT INTO market_indices (...) VALUES (...)
     ON CONFLICT(symbol) DO UPDATE SET
       current_price=excluded.current_price,
       change_amount=excluded.change_amount,
       change_percent=excluded.change_percent,
       prev_close=excluded.prev_close,
       open_price=excluded.open_price,
       high_price=excluded.high_price,
       low_price=excluded.low_price,
       quote_time=excluded.quote_time,
       sparkline=excluded.sparkline,
       updated_at=CURRENT_TIMESTAMP;
     ```

### 4.2 外汇汇率写入：`exchange_rate_refresh_fast_batch`
- 抓取腾讯批量外汇行情后，直接通过 `exchange_rate_store()` 执行 SQL Upsert 写入 `exchange_rates` 表并记入 `exchange_rate_history` 表。

### 4.3 调度器节拍控制：`market_scheduler.c`
- **外汇汇率节拍**：
  - 周六、周日休市，跳过外汇自动抓取；
  - 周一至周五工作日交易时段内，按 **15~30 分钟** 触发一次批量拉取入库。
- **大盘指数节拍**：
  - 调度器在有市场开市时，按 **1~3 分钟** 驱动一次 `quote_engine_sync_indices_to_db(pool)`；
  - 全球主要股市全休市期间（除加密币外）降频为 15 分钟。

---

## 5. 后端读取侧：前端展示只读数据库 (Read Path)

### 5.1 大盘指数查询：`GET /api/market/indices`
- **实现位置**：`backend/src/interfaces/http/controllers/market_controller.c` -> `application/market/usecases.c` -> `infrastructure/repositories/market_repo_impl.c`。
- **只读 SQL**：
  ```sql
  SELECT symbol, name, category, region_flag, current_price, change_amount,
         change_percent, prev_close, open_price, high_price, low_price,
         currency, quote_time, sparkline, updated_at
  FROM market_indices
  WHERE (? IS NULL OR ? = '' OR category = ?)
  ORDER BY 
    CASE category 
      WHEN 'cn' THEN 1 
      WHEN 'hk' THEN 2 
      WHEN 'us' THEN 3 
      WHEN 'global' THEN 4 
      WHEN 'crypto' THEN 5 
      ELSE 6 
    END, symbol;
  ```
- **时钟状态注入**：
  - 调用 `quote_engine_calc_market_clocks(time(NULL), clocks, 8, &clock_count)` 计算实时时钟；
  - 组合打包为统一响应：
    ```json
    {
      "code": 0,
      "message": "ok",
      "data": {
        "updated_at": "2026-10-16 15:30:00",
        "market_clocks": [...],
        "indices": [...]
      }
    }
    ```
- **性能指标**：只读 SQLite / PostgreSQL 索引，响应时间在 **1~3 毫秒** 内，完全解耦外部网络风险。

### 5.2 汇率查询：`GET /api/market/exchange-rates`
- 直接执行 SQL 从 `exchange_rates` 表读取当前汇率映射字典：
  ```sql
  SELECT target_currency, rate FROM exchange_rates WHERE base_currency = 'CNY';
  ```
  组装为 `{ "CNY": 1.0, "USD": 6.6881, "EUR": ... }` 返回。

---

## 6. 前端看板轮询与智能休眠 (Frontend Experience)

### 6.1 分钟级轮询档位
在 `frontend/src/components/market/MarketClocksRibbon.vue` 与 `frontend/src/stores/market.ts` 中：
- `autoRefreshInterval` 默认值设为 **60**（1分钟）；
- 下拉选项更新为：
  - **1 分钟 (60s)** (推荐默认)
  - **3 分钟 (180s)**
  - **5 分钟 (300s)**
  - **关闭 (0s)**

### 6.2 全休市自适应休眠状态机 (Smart Sleep Machine)
- 前端计算属性：
  ```ts
  const isAnyEquityMarketOpen = computed(() =>
    marketStore.marketClocks.some(c => c.status === 'open' || c.status === 'pre')
  )
  ```
- **行为策略**：
  - **开盘状态**（`isAnyEquityMarketOpen == true`）：正常按设定周期（如 1 分钟）静默轮询；
  - **全休市状态**（`isAnyEquityMarketOpen == false`）：
    - 自动转入 15 分钟长周期休眠探活轮询；
    - 界面时钟栏展示友好提示标签：`所有股市闭市 (低频休眠中)`；
    - 一旦检测到下一个市场转为 `pre` 或 `open`（如美股晚间开市），自动唤醒恢复 1 分钟轮询；
  - **页面后台隐藏**：
    - `document.hidden` 时彻底暂停所有定时器，切回页面时立即唤醒并主动刷新一次。

---

## 7. 错误处理与容错机制 (Error Handling & Resilience)

1. **上游单点故障隔离**：
   - 外部数据源（如腾讯或 Yahoo）抓取失败时，数据库保留上一次成功记录；
   - 绝不因第三方超时或网络失败影响前端 API 返回，前端始终能瞬时拿到最新的持久化数据。
2. **冷启动与未同步保护**：
   - 数据库初始化迁移时自动预置 15 条基线数据，系统冷启动即拥有完整展示。
3. **手动一键同步无阻碍**：
   - 用户点击“立即刷新”或“实时同步汇率”，不受休市休眠限制，直接触发同步流程并写库返回。

---

## 8. 测试与质量保证 (Testing & Verification Plan)

### 8.1 单元测试（Backend CTest）
1. `test_migration_engine`：验证 `V013` 脚本在 SQLite 与 Postgres 下正确建表与预置行落库；
2. `test_market_indices`：
   - 验证 `market_repo_get_indices` 纯数据库读取功能与分类过滤；
   - 验证 `quote_engine_sync_indices_to_db` 闭市市场跳过外部抓取与数据落库行为；
3. `test_fx`：验证 `GET /api/market/exchange-rates` 纯读数据库。

### 8.2 单元测试（Frontend Vitest）
1. `tests/market-store.spec.ts`：
   - 验证 `autoRefreshInterval` 默认值为 60s；
   - 验证全休市状态下定时器休眠与开市唤醒逻辑。

### 8.3 集成测试（Integration Tests）
- 执行 `./tests/test_market_sync.sh` 与 `./tests/test_link.sh`，确保 100% 通过且无回归缺陷。
