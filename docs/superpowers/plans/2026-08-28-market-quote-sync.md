# Automated Market Quote & Net Value Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an automated market quote and net value synchronization subsystem in Minefolio, enabling smart symbol search, multi-market live quote fetching (Mutual Funds, A-shares, US/HK stocks, Crypto, Gold), asset value auto-linkage, daily price history recording, and background scheduling.

**Architecture:** A driver-based quote engine in C23 with pluggable sources (Eastmoney, Tencent, Crypto/Binance), integrated with a background scheduler and SQLite/Postgres persistence, exposed via REST APIs to a Vue 3 + TypeScript frontend featuring symbol autocomplete, one-click sync, and historical price sparklines.

**Tech Stack:** C23, csilk HTTP framework, libcurl, SQLite/PostgreSQL, Vue 3, TypeScript, Vite, Element Plus, ECharts.

---

### Task 1: Database Schema Migration & Model Extensions

**Files:**
- Modify: `backend/sql/migration.sql:29-45`
- Modify: `backend/sql/migration_postgres.sql:29-45`
- Modify: `backend/src/common/db.c:50-75`
- Test: `backend/tests/test_link.sh`

- [ ] **Step 1: Update SQLite and PostgreSQL migration scripts**

In `backend/sql/migration.sql`:
```sql
-- In assets table definition:
CREATE TABLE IF NOT EXISTS assets (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category_id   INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    name          TEXT NOT NULL,
    account_no    TEXT,
    symbol        TEXT DEFAULT '',
    quote_source  TEXT DEFAULT '',
    last_sync_at  TIMESTAMP,
    current_value DECIMAL(18,2) DEFAULT 0,
    quantity      DECIMAL(18,4) NOT NULL DEFAULT 0,
    cost_basis    DECIMAL(18,4) NOT NULL DEFAULT 0,
    net_value     DECIMAL(18,4) NOT NULL DEFAULT 0,
    currency      TEXT DEFAULT 'CNY',
    note          TEXT,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

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

In `backend/sql/migration_postgres.sql`:
```sql
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

- [ ] **Step 2: Add dynamic migration step in `db_init` in `backend/src/common/db.c`**

Add migration checks for existing databases:
```c
/* Ensure symbol, quote_source, last_sync_at columns exist on assets table */
csilk_db_exec(g_pool, "ALTER TABLE assets ADD COLUMN symbol TEXT DEFAULT '';");
csilk_db_exec(g_pool, "ALTER TABLE assets ADD COLUMN quote_source TEXT DEFAULT '';");
csilk_db_exec(g_pool, "ALTER TABLE assets ADD COLUMN last_sync_at TIMESTAMP;");
csilk_db_exec(g_pool, 
    "CREATE TABLE IF NOT EXISTS asset_price_history ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE, "
    "price_date DATE NOT NULL, "
    "price DECIMAL(18,4) NOT NULL, "
    "currency TEXT DEFAULT 'CNY', "
    "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
    "UNIQUE(asset_id, price_date));");
csilk_db_exec(g_pool, "CREATE INDEX IF NOT EXISTS idx_price_history_asset_date ON asset_price_history(asset_id, price_date DESC);");
```

- [ ] **Step 3: Compile and verify existing tests pass**

Run:
```bash
cmake --build backend/build --parallel
```
Expected: Build passes with exit code 0.

- [ ] **Step 4: Commit schema changes**

```bash
git add backend/sql/migration.sql backend/sql/migration_postgres.sql backend/src/common/db.c
git commit -m "feat(market): ✨ add symbol, quote_source and asset_price_history schema"
```

---

### Task 2: Market Types & Repositories

**Files:**
- Create: `backend/src/common/market_types.h`
- Create: `backend/src/repositories/price_history_repo.h`
- Create: `backend/src/repositories/price_history_repo.c`
- Modify: `backend/src/repositories/asset_repo.h`
- Modify: `backend/src/repositories/asset_repo.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Define market data types in `backend/src/common/market_types.h`**

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char   symbol[64];
    char   name[128];
    char   source[32];
    double current_price;
    double change_percent;
    char   currency[16];
    char   quote_time[32];
} market_quote_t;

typedef struct {
    char symbol[64];
    char name[128];
    char source[32];
    char market_desc[64];
    double current_price;
    char currency[16];
} market_search_item_t;
```

- [ ] **Step 2: Implement `price_history_repo`**

Header `backend/src/repositories/price_history_repo.h`:
```c
#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

int price_history_record(csilk_db_pool_t* pool, int64_t asset_id, const char* price_date, double price, const char* currency);
csilk_json_t* price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit);
```

Implementation `backend/src/repositories/price_history_repo.c`:
```c
#include "repositories/price_history_repo.h"
#include <stdio.h>

int
price_history_record(csilk_db_pool_t* pool, int64_t asset_id, const char* price_date, double price, const char* currency)
{
    char aid_str[32], price_str[64];
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(price_str, sizeof(price_str), "%.4f", price);
    const char* cur = (currency && currency[0]) ? currency : "CNY";
    const char* pdate = (price_date && price_date[0]) ? price_date : "date('now')";

    const char* sql = "INSERT INTO asset_price_history (asset_id, price_date, price, currency) "
                      "VALUES (?, ?, ?, ?) "
                      "ON CONFLICT(asset_id, price_date) DO UPDATE SET price=excluded.price, currency=excluded.currency";
    return csilk_db_exec_param(pool, sql, (const char*[]){aid_str, pdate, price_str, cur, NULL});
}

csilk_json_t*
price_history_list_by_asset(csilk_db_pool_t* pool, int64_t user_id, int64_t asset_id, int limit)
{
    char uid_str[32], aid_str[32], lim_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    snprintf(aid_str, sizeof(aid_str), "%lld", (long long)asset_id);
    snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 90);

    const char* sql = "SELECT h.id, h.asset_id, h.price_date, h.price, h.currency, h.created_at "
                      "FROM asset_price_history h "
                      "JOIN assets a ON h.asset_id = a.id "
                      "WHERE a.user_id = ? AND h.asset_id = ? "
                      "ORDER BY h.price_date ASC LIMIT ?";
    return csilk_db_query_param_json(pool, sql, (const char*[]){uid_str, aid_str, lim_str, NULL});
}
```

- [ ] **Step 3: Update `asset_repo` to support symbol, quote_source and market quote updates**

In `backend/src/repositories/asset_repo.h` and `backend/src/repositories/asset_repo.c`:
- Add `symbol`, `quote_source`, `last_sync_at` to `asset_insert`, `asset_update`, `asset_list`, `asset_get`.
- Add `asset_update_market_quote(pool, asset_id, user_id, new_net_value)` which atomically updates `net_value`, recalculates `current_value = quantity * net_value` if `quantity > 0`, and updates `last_sync_at = datetime('now')`.

- [ ] **Step 4: Update `CMakeLists.txt`, compile and verify**

Add `src/repositories/price_history_repo.c` to `CMakeLists.txt`.
Run:
```bash
cmake --build backend/build --parallel
```
Expected: Build passes with exit code 0.

- [ ] **Step 5: Commit repository implementations**

```bash
git add backend/src/common/market_types.h backend/src/repositories/price_history_repo.h backend/src/repositories/price_history_repo.c backend/src/repositories/asset_repo.h backend/src/repositories/asset_repo.c backend/CMakeLists.txt
git commit -m "feat(market): ✨ add price history repo and asset quote update repository methods"
```

---

### Task 3: Market Quote Drivers & Engine

**Files:**
- Create: `backend/src/services/market/quote_driver.h`
- Create: `backend/src/services/market/driver_eastmoney.c`
- Create: `backend/src/services/market/driver_tencent.c`
- Create: `backend/src/services/market/driver_crypto.c`
- Create: `backend/src/services/market/quote_engine.h`
- Create: `backend/src/services/market/quote_engine.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Define driver interface in `backend/src/services/market/quote_driver.h`**

```c
#pragma once
#include "common/market_types.h"

typedef struct quote_driver_s quote_driver_t;

struct quote_driver_s {
    const char* name;
    const char* source_type;
    int (*search)(const char* keyword, market_search_item_t* out_items, int max_items);
    int (*fetch_single)(const char* symbol, market_quote_t* out_quote);
    int (*fetch_batch)(const char** symbols, int count, market_quote_t* out_quotes, int* out_count);
};

/* Driver registrations */
quote_driver_t* get_eastmoney_driver(void);
quote_driver_t* get_tencent_driver(void);
quote_driver_t* get_crypto_driver(void);
```

- [ ] **Step 2: Implement Eastmoney driver for mutual funds (`driver_eastmoney.c`)**

- Search endpoint: `https://fundsuggest.eastmoney.com/FundSearch/api/FundSearchAPI.ashx?m=1&key={keyword}`
- Quote endpoint: `http://fundgz.1234567.com.cn/js/{code}.js`
- Parses `dwjz` (NAV), `gsz` (real-time estimate), `jzrq` (date), `gszzl` (change %).

- [ ] **Step 3: Implement Tencent driver for A-shares, US, HK, Gold (`driver_tencent.c`)**

- Multi-quote endpoint: `http://qt.gtimg.cn/q={symbols}`
- Supports batch fetching `q=sh600519,sz000001,usAAPL,hk00700,s_au9999`.
- Parses stock name, current price, change percentage, date/time.

- [ ] **Step 4: Implement Crypto driver for Binance/CoinGecko (`driver_crypto.c`)**

- Ticker endpoint: `https://api.binance.com/api/v3/ticker/24hr?symbol={symbol}`
- Parses last price and priceChangePercent.

- [ ] **Step 5: Implement `quote_engine.c`**

- Implements proxy configuration (`market_set_proxy`, `market_http_get`).
- Dispatches search requests across all active drivers and merges search results.
- Dispatches batch quote synchronization by source type.

- [ ] **Step 6: Update `CMakeLists.txt`, compile and write driver unit test**

Run:
```bash
cmake --build backend/build --parallel
```
Expected: Build passes with exit code 0.

- [ ] **Step 7: Commit market drivers and quote engine**

```bash
git add backend/src/services/market/ backend/CMakeLists.txt
git commit -m "feat(market): ✨ implement market quote drivers and unified quote engine"
```

---

### Task 4: Market Service, Settings & Controller Routes

**Files:**
- Create: `backend/src/services/market_service.h`
- Create: `backend/src/services/market_service.c`
- Create: `backend/src/controllers/market_controller.h`
- Create: `backend/src/controllers/market_controller.c`
- Modify: `backend/src/main.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Implement `market_service`**

Header `backend/src/services/market_service.h`:
```c
#pragma once
#include "csilk/csilk.h"

void market_service_search(csilk_ctx_t* c);
void market_service_sync(csilk_ctx_t* c);
void market_service_sync_single(csilk_ctx_t* c);
void market_service_get_history(csilk_ctx_t* c);
void market_service_get_settings(csilk_ctx_t* c);
void market_service_update_settings(csilk_ctx_t* c);
void market_service_test_connection(csilk_ctx_t* c);
```

Implementation `backend/src/services/market_service.c`:
- `market_service_search`: Parses `keyword` query param, calls `quote_engine_search`, returns list.
- `market_service_sync`: Queries user's assets with `symbol != ''`, invokes batch quote fetch, updates `assets.net_value`, `current_value`, and records into `asset_price_history`.
- `market_service_get_history`: Calls `price_history_list_by_asset`.
- `market_service_get_settings` / `update_settings`: Reads/updates proxy & auto-sync config in DB.

- [ ] **Step 2: Implement `market_controller` and register routes**

In `backend/src/controllers/market_controller.c`:
```c
void register_market_routes(csilk_app_t* app) {
    csilk_app_get_ext(app, "/api/market/search", market_service_search, NULL, NULL, "Search market symbols", "");
    csilk_app_post_ext(app, "/api/market/sync", market_service_sync, NULL, NULL, "Sync market quotes", "");
    csilk_app_post_ext(app, "/api/market/sync/:id", market_service_sync_single, NULL, NULL, "Sync single asset quote", "");
    csilk_app_get_ext(app, "/api/market/history/:id", market_service_get_history, NULL, NULL, "Get asset price history", "");
    csilk_app_get_ext(app, "/api/market/settings", market_service_get_settings, NULL, NULL, "Get market settings", "");
    csilk_app_put_ext(app, "/api/market/settings", market_service_update_settings, NULL, NULL, "Update market settings", "");
    csilk_app_post_ext(app, "/api/market/settings/test", market_service_test_connection, NULL, NULL, "Test market connection", "");
}
```

In `backend/src/main.c`:
Add `#include "controllers/market_controller.h"` and call `register_market_routes(app);`.

- [ ] **Step 3: Compile and test endpoints with curl**

Run:
```bash
cmake --build backend/build --parallel
```
Expected: Build passes with 0 errors.

- [ ] **Step 4: Commit market service and controller**

```bash
git add backend/src/services/market_service.* backend/src/controllers/market_controller.* backend/src/main.c backend/CMakeLists.txt
git commit -m "feat(market): ✨ add market service, controller and REST endpoints"
```

---

### Task 5: Background Scheduler (Trading Hours & Nightly Settlement)

**Files:**
- Create: `backend/src/services/market_scheduler.h`
- Create: `backend/src/services/market_scheduler.c`
- Modify: `backend/src/main.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: Implement `market_scheduler`**

Header `backend/src/services/market_scheduler.h`:
```c
#pragma once
#include <stdbool.h>

int market_scheduler_start(void);
void market_scheduler_stop(void);
```

Implementation `backend/src/services/market_scheduler.c`:
- Starts a background pthread.
- Loops every minute:
  - Checks if `market_auto_sync` is enabled in settings.
  - If current time is within trading hours (Mon-Fri 09:30-15:00 CST or 21:30-04:00 CST), checks interval timer and triggers sync for all active assets.
  - If current time is 22:00 or 23:30 CST, triggers nightly mutual fund settlement sync.

- [ ] **Step 2: Initialize scheduler in `main.c`**

Call `market_scheduler_start()` after `db_init()` in `main.c`.

- [ ] **Step 3: Compile and verify**

Run:
```bash
cmake --build backend/build --parallel
```
Expected: Build passes with 0 errors.

- [ ] **Step 4: Commit scheduler implementation**

```bash
git add backend/src/services/market_scheduler.* backend/src/main.c backend/CMakeLists.txt
git commit -m "feat(market): ✨ add background market quote scheduler for trading hours and nightly settlement"
```

---

### Task 6: Frontend API Client & TypeScript Types

**Files:**
- Modify: `frontend/src/types/index.ts`
- Create: `frontend/src/api/market.ts`

- [ ] **Step 1: Update TypeScript interfaces in `frontend/src/types/index.ts`**

Add:
```typescript
export interface MarketQuote {
  symbol: string
  name: string
  source: string
  current_price: number
  change_percent: number
  currency: string
  quote_time: string
}

export interface MarketSearchItem {
  symbol: string
  name: string
  source: string
  market_desc: string
  current_price: number
  currency: string
}

export interface AssetPriceHistory {
  id: number
  asset_id: number
  price_date: string
  price: number
  currency: string
  created_at: string
}

export interface MarketSettings {
  market_proxy: string
  market_auto_sync: boolean
  market_sync_interval_min: number
}

export interface Asset {
  id: number
  category_id: number
  name: string
  account_no?: string
  symbol?: string
  quote_source?: string
  last_sync_at?: string
  current_value: number
  quantity: number
  cost_basis: number
  net_value: number
  currency: string
  note?: string
  created_at?: string
  updated_at?: string
}
```

- [ ] **Step 2: Create `frontend/src/api/market.ts`**

```typescript
import http from '@/utils/http'
import type { MarketSearchItem, MarketQuote, AssetPriceHistory, MarketSettings } from '@/types'

export const marketApi = {
  search(keyword: string) {
    return http.get<{ list: MarketSearchItem[] }>('/api/market/search', { params: { keyword } })
  },
  syncAll(asset_ids?: number[]) {
    return http.post<{ total: number; updated: number; failed: number }>('/api/market/sync', { asset_ids })
  },
  syncSingle(assetId: number) {
    return http.post<MarketQuote>(`/api/market/sync/${assetId}`)
  },
  getHistory(assetId: number, limit = 90) {
    return http.get<{ list: AssetPriceHistory[] }>(`/api/market/history/${assetId}`, { params: { limit } })
  },
  getSettings() {
    return http.get<MarketSettings>('/api/market/settings')
  },
  updateSettings(data: MarketSettings) {
    return http.put('/api/market/settings', data)
  },
  testConnection() {
    return http.post<{ status: string; latency_ms: number }>('/api/market/settings/test')
  }
}
```

- [ ] **Step 3: Verify TypeScript compilation**

Run:
```bash
npm --prefix frontend run build
```
Expected: Compiles with 0 errors.

- [ ] **Step 4: Commit frontend API and types**

```bash
git add frontend/src/types/index.ts frontend/src/api/market.ts
git commit -m "feat(market): ✨ add market frontend api client and typescript interfaces"
```

---

### Task 7: Frontend UI (Symbol Autocomplete, Holdings Sync & Price Trend Chart)

**Files:**
- Create: `frontend/src/components/AssetPriceChart.vue`
- Modify: `frontend/src/views/Holdings.vue`
- Modify: `frontend/src/views/Assets.vue`
- Modify: `frontend/src/views/Settings.vue`

- [ ] **Step 1: Create `AssetPriceChart.vue` component**

- Uses ECharts to render responsive historical price trend line chart with tooltip and data zoom.
- Receives `assetId` and loads data via `marketApi.getHistory(assetId)`.

- [ ] **Step 2: Update `Holdings.vue` and `Assets.vue`**

- Add symbol autocomplete to Asset/Holding dialog using `el-autocomplete` querying `marketApi.search`.
- Auto-fill `name`, `symbol`, `quote_source`, `currency`, `net_value` upon selection.
- In `Holdings.vue` header: Add `🔄 同步行情 (Sync Quotes)` button with loading state.
- In `Holdings.vue` table: Add last sync time badge, quote change tag, and single-row sync button.
- Add click-to-view price trend dialog with `AssetPriceChart`.

- [ ] **Step 3: Update `Settings.vue`**

- Add "行情与同步配置 (Market Quote Settings)" card in Settings.
- Support proxy configuration, auto-sync toggle, interval selector, and "测试行情源连通性" button.

- [ ] **Step 4: Build and test frontend**

Run:
```bash
npm --prefix frontend run build
```
Expected: Build passes with 0 errors.

- [ ] **Step 5: Commit frontend UI changes**

```bash
git add frontend/src/components/AssetPriceChart.vue frontend/src/views/Holdings.vue frontend/src/views/Assets.vue frontend/src/views/Settings.vue
git commit -m "feat(market): ✨ add symbol autocomplete, holdings quote sync, price chart and market settings ui"
```

---

### Task 8: End-to-End System Verification & Integration Test Suite

**Files:**
- Create: `backend/tests/test_market_sync.sh`
- Test: Full integration test suite

- [ ] **Step 1: Create integration test script `test_market_sync.sh`**

Test scenarios:
1. `GET /api/market/search?keyword=110011` -> returns Eastmoney fund search results.
2. `GET /api/market/search?keyword=600519` -> returns A-share search results.
3. Create asset with `symbol="110011"`, `quote_source="fund_cn"`, `quantity=100`.
4. `POST /api/market/sync` -> updates `net_value`, `current_value`, `last_sync_at`.
5. Verify `asset_price_history` has recorded row for this asset.
6. `GET /api/market/history/:id` -> returns price history records.
7. Update settings with proxy and test connection.

- [ ] **Step 2: Run test suite**

Run:
```bash
chmod +x backend/tests/test_market_sync.sh && ./backend/tests/test_market_sync.sh
```
Expected: All test cases pass with status 200 and exit code 0.

- [ ] **Step 3: Deploy to Docker container and verify UI**

Run:
```bash
cmake --build backend/build --parallel && npm --prefix frontend run build
docker cp backend/build/minefolio minefolio:/app/minefolio
docker cp frontend/dist/. minefolio-nginx:/usr/share/nginx/html/
docker restart minefolio minefolio-nginx
```
Expected: Both containers restart healthy, UI opens and executes quote sync successfully.

- [ ] **Step 4: Commit integration test suite and final changes**

```bash
git add backend/tests/test_market_sync.sh
git commit -m "test(market): ✅ add integration test suite for market quote sync subsystem"
```
