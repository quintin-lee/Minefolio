# 大盘指数与外汇汇率分钟级同步及数据库直读实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建大盘指数持久化表与严格 CQRS 读写分离架构，实现前端展示 100% 只读数据库、后台同步直接写入数据库，并将大盘与汇率同步优化为开盘期感知的分钟级节拍及全休市自动休眠。

**Architecture:** 新建 `market_indices` 数据库表（Flyway V013）统一存储全球大盘数据。写入侧由后台调度器在开盘期每分钟智能调度抓取（闭市标的跳过网络 IO）并 Upsert 写库；读取侧（`GET /api/market/indices`、`GET /api/market/exchange-rates`）只读数据库，微秒级极速响应。前端提供 1m / 3m / 5m / 关闭档位，并在全休市时自动进入低频休眠探活。

**Tech Stack:** C23, SQLite3, PostgreSQL (libpq), csilk framework, Vue 3, TypeScript, Pinia, Element Plus, Vitest, CTest.

---

## 文件变更结构映射 (File Structure)

- **数据库迁移层**
  - 新增：`backend/sql/migrations/sqlite/V013__market_indices_table.sql`
  - 新增：`backend/sql/migrations/postgres/V013__market_indices_table.sql`
  - 修改：`backend/sql/migration.sql`（追加 SQLite 全量 schema）
  - 修改：`backend/sql/migration_postgres.sql`（追加 PG 全量 schema）
- **数据仓储层 (Infrastructure Repositories)**
  - 修改：`backend/src/infrastructure/repositories/market_repo_impl.h`（声明大盘指数与汇率库读写函数）
  - 修改：`backend/src/infrastructure/repositories/market_repo_impl.c`（实现大盘读取、Upsert 与汇率直查）
- **行情引擎与调度服务 (Services Layer)**
  - 修改：`backend/src/services/market/quote_engine.h`（导出入库同步与时钟感知函数）
  - 修改：`backend/src/services/market/quote_engine.c`（实现 `quote_engine_sync_indices_to_db`，闭市跳过 IO）
  - 修改：`backend/src/services/market/market_scheduler.c`（开盘期分钟级驱动大盘入库，工作日低频刷新汇率）
- **应用用例与接口控制层 (Application & Controllers)**
  - 修改：`backend/src/application/market/usecases.h`（更新用例签名入参 pool）
  - 修改：`backend/src/application/market/usecases.c`（`market_usecase_get_indices` 与 `market_usecase_get_exchange_rates` 切换为纯库查询）
  - 修改：`backend/src/interfaces/http/controllers/market_controller.c`（传递 `db_get_pool()`）
- **前端状态与视图层 (Frontend Store & Components)**
  - 修改：`frontend/src/stores/market.ts`（默认 60s 轮询，支持休市自适应）
  - 修改：`frontend/src/components/market/MarketClocksRibbon.vue`（提供 1m/3m/5m/off 选项与休眠提示标签）
  - 修改：`frontend/src/components/market/GlobalIndicesBoard.vue`（接入全休市自适应休眠状态机）
- **自动化测试套件**
  - 新增/修改：`backend/tests/unit/test_market_indices.c`（测试 V013 库读写、闭市跳过）
  - 修改：`frontend/tests/market-indices.spec.ts`（测试 60s 默认与休眠状态机）
  - 修改：`frontend/tests/market-store.spec.ts`

---

### Task 1: 数据库迁移 V013 — 新增 `market_indices` 表与基线数据

**Files:**
- Create: `backend/sql/migrations/sqlite/V013__market_indices_table.sql`
- Create: `backend/sql/migrations/postgres/V013__market_indices_table.sql`
- Modify: `backend/sql/migration.sql`
- Modify: `backend/sql/migration_postgres.sql`
- Test: `backend/tests/unit/test_migration_engine.c`

- [ ] **Step 1: 编写 SQLite 迁移脚本 `V013__market_indices_table.sql`**

```sql
-- backend/sql/migrations/sqlite/V013__market_indices_table.sql
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

-- 预置 15 条基线大盘标的
INSERT OR IGNORE INTO market_indices (symbol, name, category, region_flag, current_price, change_amount, change_percent, prev_close, open_price, high_price, low_price, currency, quote_time, sparkline) VALUES
('sh000001', '上证指数', 'cn', '🇨🇳', 3285.40, 25.40, 0.78, 3260.00, 3270.00, 3295.00, 3265.00, 'CNY', '2026-10-16 15:00:00', '[3265,3270,3275,3280,3278,3282,3284,3285.4]'),
('sz399001', '深证成指', 'cn', '🇨🇳', 10550.20, 54.50, 0.52, 10495.70, 10510.00, 10580.00, 10500.00, 'CNY', '2026-10-16 15:00:00', '[10500,10510,10520,10540,10530,10545,10548,10550.2]'),
('sz399006', '创业板指', 'cn', '🇨🇳', 2188.60, 24.80, 1.15, 2163.80, 2170.00, 2195.00, 2168.00, 'CNY', '2026-10-16 15:00:00', '[2168,2170,2175,2180,2178,2184,2186,2188.6]'),
('sh000300', '沪深300', 'cn', '🇨🇳', 3920.10, 25.20, 0.65, 3894.90, 3905.00, 3930.00, 3900.00, 'CNY', '2026-10-16 15:00:00', '[3900,3905,3910,3915,3912,3918,3919,3920.1]'),
('hkHSI', '恒生指数', 'hk', '🇭🇰', 20630.00, -51.80, -0.25, 20681.80, 20650.00, 20720.00, 20600.00, 'HKD', '2026-10-16 16:00:00', '[20680,20670,20650,20640,20620,20630,20635,20630]'),
('hkHSTECH', '恒生科技', 'hk', '🇭🇰', 4520.50, 18.90, 0.42, 4501.60, 4510.00, 4535.00, 4495.00, 'HKD', '2026-10-16 16:00:00', '[4500,4510,4515,4520,4512,4518,4522,4520.5]'),
('^GSPC', '标普 500', 'us', '🇺🇸', 5864.67, 24.50, 0.42, 5840.17, 5845.00, 5875.00, 5835.00, 'USD', '2026-10-16 16:00:00', '[5840,5845,5850,5855,5860,5862,5863,5864.67]'),
('^IXIC', '纳斯达克', 'us', '🇺🇸', 18373.61, -33.10, -0.18, 18406.71, 18390.00, 18420.00, 18350.00, 'USD', '2026-10-16 16:00:00', '[18400,18390,18385,18370,18365,18370,18372,18373.61]'),
('^DJI', '道琼斯', 'us', '🇺🇸', 43065.22, 150.20, 0.35, 42915.02, 42950.00, 43120.00, 42920.00, 'USD', '2026-10-16 16:00:00', '[42920,42950,42980,43010,43030,43050,43060,43065.22]'),
('^N225', '日经 225', 'global', '🇯🇵', 39180.30, 341.20, 0.88, 38839.10, 38900.00, 39250.00, 38880.00, 'JPY', '2026-10-16 15:00:00', '[38850,38900,38980,39050,39120,39150,39170,39180.3]'),
('^GDAXI', '德国 DAX', 'global', '🇩🇪', 19580.40, 41.10, 0.21, 19539.30, 19550.00, 19610.00, 19530.00, 'EUR', '2026-10-16 17:30:00', '[19540,19550,19560,19570,19565,19575,19578,19580.4]'),
('^FTSE', '英国富时', 'global', '🇬🇧', 8240.10, -4.10, -0.05, 8244.20, 8250.00, 8265.00, 8230.00, 'GBP', '2026-10-16 16:30:00', '[8245,8250,8248,8242,8238,8240,8239,8240.1]'),
('BTC-USD', '比特币', 'crypto', '🪙', 67800.00, 1620.00, 2.45, 66180.00, 66200.00, 68200.00, 66100.00, 'USD', '2026-10-16 15:30:00', '[66200,66500,66800,67100,67400,67600,67750,67800]'),
('ETH-USD', '以太坊', 'crypto', '🪙', 2640.00, 47.20, 1.82, 2592.80, 2600.00, 2660.00, 2590.00, 'USD', '2026-10-16 15:30:00', '[2595,2605,2615,2625,2630,2635,2638,2640]'),
('GC=F', '黄金现货', 'crypto', '🪙', 2680.50, 10.10, 0.38, 2670.40, 2672.00, 2688.00, 2670.00, 'USD', '2026-10-16 15:30:00', '[2671,2673,2675,2678,2676,2679,2680,2680.5]');
```

- [ ] **Step 2: 编写 PostgreSQL 迁移脚本 `V013__market_indices_table.sql`**

```sql
-- backend/sql/migrations/postgres/V013__market_indices_table.sql
CREATE TABLE IF NOT EXISTS market_indices (
    symbol          VARCHAR(32) PRIMARY KEY,
    name            VARCHAR(64) NOT NULL,
    category        VARCHAR(32) NOT NULL,
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
    sparkline       TEXT NOT NULL DEFAULT '[]',
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_market_indices_cat ON market_indices(category);

INSERT INTO market_indices (symbol, name, category, region_flag, current_price, change_amount, change_percent, prev_close, open_price, high_price, low_price, currency, quote_time, sparkline) VALUES
('sh000001', '上证指数', 'cn', '🇨🇳', 3285.40, 25.40, 0.78, 3260.00, 3270.00, 3295.00, 3265.00, 'CNY', '2026-10-16 15:00:00', '[3265,3270,3275,3280,3278,3282,3284,3285.4]'),
('sz399001', '深证成指', 'cn', '🇨🇳', 10550.20, 54.50, 0.52, 10495.70, 10510.00, 10580.00, 10500.00, 'CNY', '2026-10-16 15:00:00', '[10500,10510,10520,10540,10530,10545,10548,10550.2]'),
('sz399006', '创业板指', 'cn', '🇨🇳', 2188.60, 24.80, 1.15, 2163.80, 2170.00, 2195.00, 2168.00, 'CNY', '2026-10-16 15:00:00', '[2168,2170,2175,2180,2178,2184,2186,2188.6]'),
('sh000300', '沪深300', 'cn', '🇨🇳', 3920.10, 25.20, 0.65, 3894.90, 3905.00, 3930.00, 3900.00, 'CNY', '2026-10-16 15:00:00', '[3900,3905,3910,3915,3912,3918,3919,3920.1]'),
('hkHSI', '恒生指数', 'hk', '🇭🇰', 20630.00, -51.80, -0.25, 20681.80, 20650.00, 20720.00, 20600.00, 'HKD', '2026-10-16 16:00:00', '[20680,20670,20650,20640,20620,20630,20635,20630]'),
('hkHSTECH', '恒生科技', 'hk', '🇭🇰', 4520.50, 18.90, 0.42, 4501.60, 4510.00, 4535.00, 4495.00, 'HKD', '2026-10-16 16:00:00', '[4500,4510,4515,4520,4512,4518,4522,4520.5]'),
('^GSPC', '标普 500', 'us', '🇺🇸', 5864.67, 24.50, 0.42, 5840.17, 5845.00, 5875.00, 5835.00, 'USD', '2026-10-16 16:00:00', '[5840,5845,5850,5855,5860,5862,5863,5864.67]'),
('^IXIC', '纳斯达克', 'us', '🇺🇸', 18373.61, -33.10, -0.18, 18406.71, 18390.00, 18420.00, 18350.00, 'USD', '2026-10-16 16:00:00', '[18400,18390,18385,18370,18365,18370,18372,18373.61]'),
('^DJI', '道琼斯', 'us', '🇺🇸', 43065.22, 150.20, 0.35, 42915.02, 42950.00, 43120.00, 42920.00, 'USD', '2026-10-16 16:00:00', '[42920,42950,42980,43010,43030,43050,43060,43065.22]'),
('^N225', '日经 225', 'global', '🇯🇵', 39180.30, 341.20, 0.88, 38839.10, 38900.00, 39250.00, 38880.00, 'JPY', '2026-10-16 15:00:00', '[38850,38900,38980,39050,39120,39150,39170,39180.3]'),
('^GDAXI', '德国 DAX', 'global', '🇩🇪', 19580.40, 41.10, 0.21, 19539.30, 19550.00, 19610.00, 19530.00, 'EUR', '2026-10-16 17:30:00', '[19540,19550,19560,19570,19565,19575,19578,19580.4]'),
('^FTSE', '英国富时', 'global', '🇬🇧', 8240.10, -4.10, -0.05, 8244.20, 8250.00, 8265.00, 8230.00, 'GBP', '2026-10-16 16:30:00', '[8245,8250,8248,8242,8238,8240,8239,8240.1]'),
('BTC-USD', '比特币', 'crypto', '🪙', 67800.00, 1620.00, 2.45, 66180.00, 66200.00, 68200.00, 66100.00, 'USD', '2026-10-16 15:30:00', '[66200,66500,66800,67100,67400,67600,67750,67800]'),
('ETH-USD', '以太坊', 'crypto', '🪙', 2640.00, 47.20, 1.82, 2592.80, 2600.00, 2660.00, 2590.00, 'USD', '2026-10-16 15:30:00', '[2595,2605,2615,2625,2630,2635,2638,2640]'),
('GC=F', '黄金现货', 'crypto', '🪙', 2680.50, 10.10, 0.38, 2670.40, 2672.00, 2688.00, 2670.00, 'USD', '2026-10-16 15:30:00', '[2671,2673,2675,2678,2676,2679,2680,2680.5]')
ON CONFLICT (symbol) DO NOTHING;
```

- [ ] **Step 3: 同步更新 `backend/sql/migration.sql` 与 `backend/sql/migration_postgres.sql`**
将 `market_indices` 建表语句追加至完整建库脚本。

- [ ] **Step 4: 编译并执行迁移测试**
Run: `cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_migration_engine --output-on-failure`
Expected: PASS (V013 自动应用成功)

- [ ] **Step 5: 提交 Task 1 成果**
```bash
git add backend/sql/
git commit -m "feat(market): ✨ add V013 migration for market_indices persistent table"
```

---

### Task 2: 仓储层实现 — 大盘指数与外汇直查/直写 SQL

**Files:**
- Modify: `backend/src/infrastructure/repositories/market_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/market_repo_impl.c`
- Test: `backend/tests/unit/test_database_repository.c`

- [ ] **Step 1: 在 `market_repo_impl.h` 中声明直查与直写函数**

```c
csilk_json_t* mf_market_repo_get_indices(void* pool, const char* category);
int mf_market_repo_upsert_index(void* pool,
                                const char* symbol,
                                const char* name,
                                const char* category,
                                const char* region_flag,
                                double current_price,
                                double change_amount,
                                double change_percent,
                                double prev_close,
                                double open_price,
                                double high_price,
                                double low_price,
                                const char* currency,
                                const char* quote_time,
                                const char* sparkline_json);
csilk_json_t* mf_market_repo_get_all_exchange_rates(void* pool);
```

- [ ] **Step 2: 在 `market_repo_impl.c` 中实现 SQL 查询**
- `mf_market_repo_get_indices`：纯 SQL SELECT，支持 category 过滤，按 A股->港股->美股->全球->加密 排序。
- `mf_market_repo_upsert_index`：参数化 UPSERT 语句（利用 SQLite / Postgres `ON CONFLICT (symbol) DO UPDATE SET`）。
- `mf_market_repo_get_all_exchange_rates`：执行 `SELECT target_currency, rate FROM exchange_rates WHERE base_currency = 'CNY'`，转换为 `{ "CNY": 1.0, "USD": 6.6881, ... }` 的 JSON 对象。

- [ ] **Step 3: 运行单元测试验证**
Run: `cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_database_repository --output-on-failure`
Expected: PASS

- [ ] **Step 4: 提交 Task 2 成果**
```bash
git add backend/src/infrastructure/repositories/market_repo_impl.*
git commit -m "feat(market): ✨ implement market_indices and exchange_rates database repository"
```

---

### Task 3: 写入侧实现 — `quote_engine_sync_indices_to_db` 与开闭市过滤

**Files:**
- Modify: `backend/src/services/market/quote_engine.h`
- Modify: `backend/src/services/market/quote_engine.c`
- Test: `backend/tests/unit/test_market_indices.c`

- [ ] **Step 1: 在 `quote_engine.h` 中导出同步函数**

```c
int quote_engine_sync_indices_to_db(csilk_db_pool_t* pool);
```

- [ ] **Step 2: 在 `quote_engine.c` 中实现时钟感知入库逻辑**
1. 获取 5 大市场时钟状态；
2. 标的所属市场映射：
   - `cn` -> `SSE_SZSE`
   - `hk` -> `HKEX`
   - `us` -> `NYSE`
   - `global (^N225)` -> `TSE`
   - `global (^GDAXI, ^FTSE)` -> `LSE`
   - `crypto` -> 7x24 持续活跃
3. **开闭市过滤**：
   - 若标的对应时钟为 `closed`，跳过外部网络抓取；
   - 若标的为 `open`、`pre` 或 `crypto`，调用 `quote_engine_fetch_quote` 获取最新价并计算均线及 sparkline；
4. 调用 `mf_market_repo_upsert_index()` 直接 Upsert 写入数据库。

- [ ] **Step 3: 运行单元测试**
Run: `cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_market_indices --output-on-failure`
Expected: PASS

- [ ] **Step 4: 提交 Task 3 成果**
```bash
git add backend/src/services/market/quote_engine.*
git commit -m "feat(market): ✨ implement market-clock aware quote_engine_sync_indices_to_db"
```

---

### Task 4: 读取侧改造 — 用例与控制器 100% 直读数据库

**Files:**
- Modify: `backend/src/application/market/usecases.h`
- Modify: `backend/src/application/market/usecases.c`
- Modify: `backend/src/interfaces/http/controllers/market_controller.c`
- Test: `backend/tests/unit/test_market_indices.c`

- [ ] **Step 1: 改造 `market_usecase_get_indices` 为纯库读取**
- 接收 `csilk_db_pool_t* pool`；
- 调用 `mf_market_repo_get_indices(pool, category)` 获取落库记录；
- 调用纯内存计算函数 `quote_engine_calc_market_clocks(time(NULL), clocks, 8, &clock_count)` 注入实时时钟；
- 组装 `{ "updated_at": "...", "market_clocks": [...], "indices": [...] }` 返回。**0 外部网络调用**。

- [ ] **Step 2: 改造 `market_usecase_get_exchange_rates` 为纯库读取**
- 调用 `mf_market_repo_get_all_exchange_rates(pool)`，确保直接从 `exchange_rates` 数据表返回汇率，彻底移除对静态内存数组的依赖。

- [ ] **Step 3: 运行测试**
Run: `cmake --build backend/build --parallel && ctest --test-dir backend/build -R test_market_indices --output-on-failure`
Expected: PASS

- [ ] **Step 4: 提交 Task 4 成果**
```bash
git add backend/src/application/market/ backend/src/interfaces/http/controllers/market_controller.c
git commit -m "refactor(market): ♻️ switch get_indices and get_exchange_rates to 100% db queries"
```

---

### Task 5: 调度器改造 — 分钟级开市同步与工作日外汇节拍

**Files:**
- Modify: `backend/src/services/market/market_scheduler.c`
- Test: `backend/tests/test_market_sync.sh`

- [ ] **Step 1: 在 `market_scheduler.c` 中添加大盘与汇率独立节拍**
- `time_t last_indices_sync = 0;`
- `time_t last_fx_sync = 0;`
- 在 `scheduler_loop` 中：
  - **大盘同步**：每 60 秒检查一次；若有任意股票市场处于 `open`/`pre`（或包含 7x24 加密），触发 `quote_engine_sync_indices_to_db(g_pool)`；若全休市则每 15 分钟触发一次。
  - **外汇同步**：周六/周日跳过；周一至周五开市期每 15~30 分钟触发一次 `exchange_rate_refresh_all()`（直接 Upsert 入库）。

- [ ] **Step 2: 运行集成测试验证**
Run: `(cd backend && ./tests/test_market_sync.sh && ./tests/test_fx_oauth.sh)`
Expected: ALL PASS

- [ ] **Step 3: 提交 Task 5 成果**
```bash
git add backend/src/services/market/market_scheduler.c
git commit -m "feat(market): ✨ configure minute-level trading hours cadence in market_scheduler"
```

---

### Task 6: 前端看板改造 — 分钟级选项与全休市智能休眠

**Files:**
- Modify: `frontend/src/stores/market.ts`
- Modify: `frontend/src/components/market/MarketClocksRibbon.vue`
- Modify: `frontend/src/components/market/GlobalIndicesBoard.vue`
- Test: `frontend/tests/market-indices.spec.ts`
- Test: `frontend/tests/market-store.spec.ts`

- [ ] **Step 1: 更新 `frontend/src/stores/market.ts`**
- `autoRefreshInterval` 默认值改为 `60`。
- 增加 `isAnyMarketActive` 计算属性（判断是否存在 `open` 或 `pre` 的市场）。

- [ ] **Step 2: 更新 `MarketClocksRibbon.vue` 选项与休眠标签**
- 下拉选项更新为：
  - `1分钟` (60s, 默认)
  - `3分钟` (180s)
  - `5分钟` (300s)
  - `关闭` (0s)
- 在休市时显示轻量 tag：`全部闭市 (自动休眠)`。

- [ ] **Step 3: 更新 `GlobalIndicesBoard.vue` 状态机**
- 当检测到所有股市休市（`!isAnyMarketActive`），将定时器周期降至 15 分钟长周期；
- 检测到市场开盘，自动恢复为当前配置的分钟级活跃轮询。

- [ ] **Step 4: 运行前端 Vitest 测试**
Run: `npm --prefix frontend test`
Expected: 100% PASS

- [ ] **Step 5: 提交 Task 6 成果**
```bash
git add frontend/
git commit -m "feat(market): ✨ add minute refresh options and smart sleep machine on closed markets"
```

---

### Task 7: 全量验证、构建与集成测试 (Final Verification)

**Files:**
- All touched files
- Test: Full verification pipeline

- [ ] **Step 1: 编译验证（双端 0 错误）**
Run: `cmake --build backend/build --parallel && npm --prefix frontend run build && npm --prefix frontend run build:mobile`
Expected: All exit with code 0.

- [ ] **Step 2: 后端单元测试**
Run: `ctest --test-dir backend/build --output-on-failure`
Expected: 33/33 PASS (100%)

- [ ] **Step 3: 前端测试**
Run: `npm --prefix frontend test`
Expected: 20/20 files PASS, 103+ tests PASS

- [ ] **Step 4: 综合端到端集成测试**
Run: `(cd backend && ./tests/test_link.sh && ./tests/test_market_sync.sh && ./tests/test_fx_oauth.sh)`
Expected: All suites PASS (0 failure)

- [ ] **Step 5: 最终整洁提交与状态核对**
Run: `git status`
Expected: Working tree clean, everything committed.
