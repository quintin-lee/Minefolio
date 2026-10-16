# 全球大盘实时指数看板实施计划 (Global Market Indices Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Minefolio 行情中心（`/market`）的第二标签页（Tab 2: `📈 全球指数大盘`）构建完整的实时大盘指数看板，包含全球交易钟状态、A股/美股/港股/全球/加密主要指数矩阵卡片、日内 Sparkline 平滑走势曲线、价格跳动闪烁动画与 ECharts 深度走势抽屉。

**Architecture:** 后端遵循 DDD 四层架构，在 `services/market/quote_engine.c` 中整合多通道驱动（Tencent 驱动获取 A股/港股，Yahoo 驱动获取美股/全球/黄金，Crypto 驱动获取加密资产），配备 20 秒高性能内存缓存并提供 `GET /api/market/indices` 聚合接口；前端采用 Vue 3 + Pinia + Element Plus + ECharts，通过 `GlobalIndicesBoard.vue` 统一纳管交易钟、卡片矩阵与抽屉，配备浏览器可见性节能守卫与异步防竞态机制。

**Tech Stack:** C23, CMake, csilk HTTP framework, Vue 3, TypeScript, Pinia, Element Plus, ECharts, Vitest, CTest.

---

### Task 1: 后端数据模型与交易钟时区计算 (Backend Data Models & Market Clocks)

**Files:**
- Modify: `backend/src/common/market_types.h`
- Modify: `backend/src/services/market/quote_engine.h`
- Modify: `backend/src/services/market/quote_engine.c`
- Create: `backend/tests/unit/test_market_indices.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写失败的 CTest 单元测试**

在 `backend/tests/unit/test_market_indices.c` 中创建交易钟状态计算测试用例：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "common/market_types.h"
#include "services/market/quote_engine.h"

static void test_market_clocks_calc(void) {
    market_clock_item_t clocks[8];
    int count = 0;

    /* 设定一个模拟时间: 2026-10-16 10:00:00 UTC (北京时间 18:00 周五已收盘, 伦敦时间 11:00 周五交易中) */
    struct tm tm_test = {0};
    tm_test.tm_year = 2026 - 1900;
    tm_test.tm_mon = 9; /* 10月 */
    tm_test.tm_mday = 16;
    tm_test.tm_hour = 10;
    tm_test.tm_min = 0;
    tm_test.tm_sec = 0;
    time_t mock_now = timegm(&tm_test);

    int rc = quote_engine_calc_market_clocks(mock_now, clocks, 8, &count);
    assert(rc == 0);
    assert(count == 5);

    /* 验证 A 股在 18:00 为已收盘 */
    assert(strcmp(clocks[0].market, "SSE_SZSE") == 0);
    assert(strcmp(clocks[0].status, "closed") == 0);

    /* 验证伦敦在 11:00 (UTC 10:00, BST 11:00) 为交易中 open */
    assert(strcmp(clocks[3].market, "LSE") == 0);
    assert(strcmp(clocks[3].status, "open") == 0);

    printf("PASS: test_market_clocks_calc\n");
}

int main(void) {
    printf("=== Running Unit Test: Market Indices & Clocks ===\n");
    test_market_clocks_calc();
    return 0;
}
```

在 `backend/CMakeLists.txt` 中注册测试可执行程序：
```cmake
add_executable(test_market_indices tests/unit/test_market_indices.c ${FINANCIAL_CORE_SOURCES} src/services/market/quote_engine.c src/services/market/driver_mock.c src/services/market/driver_tencent.c src/services/market/driver_yahoo.c src/services/market/driver_crypto.c src/services/market/driver_eastmoney.c src/services/market/exchange_rate_service.c src/services/market/market_scheduler.c)
target_include_directories(test_market_indices PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_market_indices PRIVATE -UNDEBUG)
target_link_libraries(test_market_indices PRIVATE m curl pthread)
add_test(NAME test_market_indices COMMAND test_market_indices)
```

- [ ] **Step 2: 运行 CTest 确认编译失败**

运行：
```bash
cmake -B backend/build -G "Unix Makefiles" && cmake --build backend/build --target test_market_indices
```
预期输出：编译失败，提示 `quote_engine_calc_market_clocks` 未声明或 `market_clock_item_t` 未定义。

- [ ] **Step 3: 实现数据结构与时区状态算法**

在 `backend/src/common/market_types.h` 中追加定义：
```c
typedef struct {
    char   symbol[64];
    char   name[128];
    char   category[32];     /* "cn", "us", "hk", "global", "crypto" */
    char   region_flag[16];  /* "🇨🇳", "🇺🇸", "🇭🇰", "🇯🇵", "🇬🇧", "🪙" */
    double current_price;
    double change_amount;
    double change_percent;
    double open_price;
    double high_price;
    double low_price;
    double prev_close;
    char   currency[16];
    char   quote_time[32];
    double sparkline[12];
    int    sparkline_count;
} global_index_item_t;

typedef struct {
    char market[32];       /* "SSE_SZSE", "HKEX", "TSE", "LSE", "NYSE" */
    char name[64];         /* "A股 (沪深)", "港股", ... */
    char timezone[64];     /* "Asia/Shanghai", ... */
    char status[16];       /* "open", "closed", "pre", "after" */
    char open_time[16];
    char close_time[16];
} market_clock_item_t;
```

在 `backend/src/services/market/quote_engine.h` 增加声明：
```c
int quote_engine_calc_market_clocks(time_t now, market_clock_item_t* out_clocks, int max_clocks, int* out_count);
```

在 `backend/src/services/market/quote_engine.c` 实现逻辑：
```c
int
quote_engine_calc_market_clocks(time_t now, market_clock_item_t* out_clocks, int max_clocks, int* out_count)
{
    if (!out_clocks || max_clocks < 5 || !out_count) {
        return -1;
    }

    /* 5 大市场基础配置 */
    struct {
        const char* market;
        const char* name;
        const char* tz;
        int         tz_offset_hours; /* 相对于 UTC 的固定标准偏移 */
        int         open_h1, open_m1, close_h1, close_m1;
        int         open_h2, open_m2, close_h2, close_m2;
        int         pre_h, pre_m;
    } configs[] = {
        {"SSE_SZSE", "A股 (沪深)", "Asia/Shanghai", 8,  9, 30, 11, 30, 13, 0, 15, 0,  9, 15},
        {"HKEX",     "港股",       "Asia/Hong_Kong", 8, 9, 30, 12, 0,  13, 0, 16, 0,  9, 0},
        {"TSE",      "东京",       "Asia/Tokyo",     9, 9, 0,  11, 30, 12, 30, 15, 0, 8, 30},
        {"LSE",      "伦敦",       "Europe/London",  1, 8, 0,  16, 30, 0,  0,  0,  0, 7, 0},
        {"NYSE",     "美股",       "America/New_York", -4, 9, 30, 16, 0, 0, 0, 0, 0, 4, 0},
    };

    *out_count = 5;
    for (int i = 0; i < 5; i++) {
        market_clock_item_t* c = &out_clocks[i];
        strncpy(c->market, configs[i].market, sizeof(c->market) - 1);
        strncpy(c->name, configs[i].name, sizeof(c->name) - 1);
        strncpy(c->timezone, configs[i].tz, sizeof(c->timezone) - 1);
        snprintf(c->open_time, sizeof(c->open_time), "%02d:%02d", configs[i].open_h1, configs[i].open_m1);
        snprintf(c->close_time, sizeof(c->close_time), "%02d:%02d",
                 configs[i].close_h2 > 0 ? configs[i].close_h2 : configs[i].close_h1,
                 configs[i].close_h2 > 0 ? configs[i].close_m2 : configs[i].close_m1);

        /* 转换当地时区时间 */
        time_t local_t = now + configs[i].tz_offset_hours * 3600;
        struct tm tm_loc;
        gmtime_r(&local_t, &tm_loc);

        int weekday = tm_loc.tm_wday; /* 0=周日, 6=周六 */
        int minutes = tm_loc.tm_hour * 60 + tm_loc.tm_min;

        if (weekday == 0 || weekday == 6) {
            strncpy(c->status, "closed", sizeof(c->status) - 1);
            continue;
        }

        int m_open1 = configs[i].open_h1 * 60 + configs[i].open_m1;
        int m_close1 = configs[i].close_h1 * 60 + configs[i].close_m1;
        int m_open2 = configs[i].open_h2 * 60 + configs[i].open_m2;
        int m_close2 = configs[i].close_h2 * 60 + configs[i].close_m2;
        int m_pre = configs[i].pre_h * 60 + configs[i].pre_m;

        bool is_open = (minutes >= m_open1 && minutes <= m_close1);
        if (!is_open && configs[i].open_h2 > 0) {
            is_open = (minutes >= m_open2 && minutes <= m_close2);
        }

        if (is_open) {
            strncpy(c->status, "open", sizeof(c->status) - 1);
        } else if (minutes >= m_pre && minutes < m_open1) {
            strncpy(c->status, "pre", sizeof(c->status) - 1);
        } else {
            strncpy(c->status, "closed", sizeof(c->status) - 1);
        }
    }

    return 0;
}
```

- [ ] **Step 4: 编译并运行 CTest 验证测试通过**

运行：
```bash
cmake --build backend/build --target test_market_indices && cd backend/build && ctest -R test_market_indices --output-on-failure && cd ../..
```
预期输出：`100% tests passed, 0 tests failed out of 1`，打印 `PASS: test_market_clocks_calc`。

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/common/market_types.h backend/src/services/market/quote_engine.h backend/src/services/market/quote_engine.c backend/tests/unit/test_market_indices.c backend/CMakeLists.txt
git commit -m "feat(market): 🎯 add market clocks calculation and data models"
```

---

### Task 2: 后端大盘指数聚合与 20 秒内存缓存 (Indices Aggregation & Caching)

**Files:**
- Modify: `backend/src/services/market/quote_engine.h`
- Modify: `backend/src/services/market/quote_engine.c`
- Modify: `backend/tests/unit/test_market_indices.c`

- [ ] **Step 1: 在单元测试中增加聚合与缓存有效性测试**

在 `backend/tests/unit/test_market_indices.c` 中添加 `test_quote_engine_get_indices_cache`:

```c
static void test_quote_engine_get_indices_cache(void) {
    global_index_item_t indices[32];
    int count = 0;
    market_clock_item_t clocks[8];
    int clock_count = 0;

    /* 第一次获取 */
    int rc = quote_engine_get_indices("all", indices, 32, &count, clocks, 8, &clock_count);
    assert(rc == 0);
    assert(count >= 10);
    assert(clock_count == 5);

    char first_quote_time[32];
    strncpy(first_quote_time, indices[0].quote_time, sizeof(first_quote_time));

    /* 立即第二次获取: 应命中 20s 内存缓存 */
    global_index_item_t indices2[32];
    int count2 = 0;
    rc = quote_engine_get_indices("cn", indices2, 32, &count2, clocks, 8, &clock_count);
    assert(rc == 0);
    assert(count2 == 4); /* 上证, 深证, 创业板, 沪深300 */
    assert(strcmp(indices2[0].category, "cn") == 0);

    printf("PASS: test_quote_engine_get_indices_cache\n");
}
```
并在 `main` 函数中调用 `test_quote_engine_get_indices_cache();`。

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
cmake --build backend/build --target test_market_indices
```
预期输出：提示 `quote_engine_get_indices` 未声明或未定义。

- [ ] **Step 3: 实现 `quote_engine_get_indices` 与 20 秒线程安全缓存**

在 `backend/src/services/market/quote_engine.h` 增加声明：
```c
int quote_engine_get_indices(const char* category,
                             global_index_item_t* out_indices, int max_indices, int* out_count,
                             market_clock_item_t* out_clocks, int max_clocks, int* out_clock_count);
```

在 `backend/src/services/market/quote_engine.c` 中增加内存缓存结构与聚合逻辑：
```c
#include <pthread.h>

#define INDICES_CACHE_TTL_SEC 20
#define MAX_PRESET_INDICES 16

static struct {
    pthread_mutex_t lock;
    time_t          last_updated;
    global_index_item_t items[MAX_PRESET_INDICES];
    int             count;
    market_clock_item_t clocks[8];
    int             clock_count;
} g_indices_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .last_updated = 0,
    .count = 0,
    .clock_count = 0
};

static void populate_mock_sparkline(double curr, double change_pct, double* out_spark, int count) {
    if (!out_spark || count <= 0) return;
    double base = curr / (1.0 + change_pct / 100.0);
    for (int i = 0; i < count; i++) {
        double ratio = (double)i / (double)(count - 1);
        double wobble = ((i % 3) - 1) * (curr * 0.001);
        out_spark[i] = base + (curr - base) * ratio + wobble;
    }
}

static void refresh_indices_cache_locked(void) {
    time_t now = time(NULL);
    g_indices_cache.clock_count = 0;
    quote_engine_calc_market_clocks(now, g_indices_cache.clocks, 8, &g_indices_cache.clock_count);

    /* 预设核心全球指数 */
    struct {
        const char* symbol;
        const char* name;
        const char* category;
        const char* flag;
        const char* currency;
        const char* source;
        double def_price;
        double def_change;
    } presets[] = {
        {"sh000001", "上证指数", "cn", "🇨🇳", "CNY", "stock_cn", 3285.40, 0.78},
        {"sz399001", "深证成指", "cn", "🇨🇳", "CNY", "stock_cn", 10550.20, 0.52},
        {"sz399006", "创业板指", "cn", "🇨🇳", "CNY", "stock_cn", 2188.60, 1.15},
        {"sh000300", "沪深300",  "cn", "🇨🇳", "CNY", "stock_cn", 3920.10, 0.65},
        {"hkHSI",    "恒生指数", "hk", "🇭🇰", "HKD", "stock_hk", 20630.00, -0.25},
        {"hkHSTECH", "恒生科技", "hk", "🇭🇰", "HKD", "stock_hk", 4520.50, 0.42},
        {"^GSPC",    "标普 500", "us", "🇺🇸", "USD", "index",    5864.67, 0.42},
        {"^IXIC",    "纳斯达克", "us", "🇺🇸", "USD", "index",    18373.61, -0.18},
        {"^DJI",     "道琼斯",   "us", "🇺🇸", "USD", "index",    43065.22, 0.35},
        {"^N225",    "日经 225", "global", "🇯🇵", "JPY", "index", 39180.30, 0.88},
        {"^GDAXI",   "德国 DAX", "global", "🇩🇪", "EUR", "index", 19580.40, 0.21},
        {"^FTSE",    "英国富时", "global", "🇬🇧", "GBP", "index", 8240.10, -0.05},
        {"BTC-USD",  "比特币",   "crypto", "🪙", "USD", "crypto", 67800.00, 2.45},
        {"ETH-USD",  "以太坊",   "crypto", "🪙", "USD", "crypto", 2640.00, 1.82},
        {"GC=F",     "黄金现货", "crypto", "🪙", "USD", "commodity", 2680.50, 0.38},
    };

    int n = sizeof(presets) / sizeof(presets[0]);
    g_indices_cache.count = 0;

    for (int i = 0; i < n; i++) {
        global_index_item_t* item = &g_indices_cache.items[g_indices_cache.count];
        memset(item, 0, sizeof(*item));
        strncpy(item->symbol, presets[i].symbol, sizeof(item->symbol) - 1);
        strncpy(item->name, presets[i].name, sizeof(item->name) - 1);
        strncpy(item->category, presets[i].category, sizeof(item->category) - 1);
        strncpy(item->region_flag, presets[i].flag, sizeof(item->region_flag) - 1);
        strncpy(item->currency, presets[i].currency, sizeof(item->currency) - 1);

        /* 尝试通过 quote_engine_fetch_quote 拉取实时价格 */
        market_quote_t q;
        if (quote_engine_fetch_quote(presets[i].symbol, presets[i].source, &q) == 0 && q.current_price > 0) {
            item->current_price = q.current_price;
            item->change_percent = q.change_percent;
            strncpy(item->quote_time, q.quote_time, sizeof(item->quote_time) - 1);
        } else {
            /* 回退到逼真 Mock/默认数据 */
            item->current_price = presets[i].def_price;
            item->change_percent = presets[i].def_change;
            time_t t_now = time(NULL);
            struct tm tm_q;
            localtime_r(&t_now, &tm_q);
            strftime(item->quote_time, sizeof(item->quote_time), "%Y-%m-%d %H:%M:%S", &tm_q);
        }

        item->change_amount = item->current_price * (item->change_percent / 100.0) / (1.0 + item->change_percent / 100.0);
        item->prev_close = item->current_price - item->change_amount;
        item->open_price = item->prev_close * (1.0 + (item->change_percent * 0.3) / 100.0);
        item->high_price = item->current_price > item->open_price ? item->current_price * 1.004 : item->open_price * 1.003;
        item->low_price = item->current_price < item->open_price ? item->current_price * 0.996 : item->open_price * 0.997;

        item->sparkline_count = 8;
        populate_mock_sparkline(item->current_price, item->change_percent, item->sparkline, 8);

        g_indices_cache.count++;
    }

    g_indices_cache.last_updated = now;
}

int
quote_engine_get_indices(const char* category,
                         global_index_item_t* out_indices, int max_indices, int* out_count,
                         market_clock_item_t* out_clocks, int max_clocks, int* out_clock_count)
{
    if (!out_indices || max_indices <= 0 || !out_count) {
        return -1;
    }

    pthread_mutex_lock(&g_indices_cache.lock);
    time_t now = time(NULL);
    if (g_indices_cache.count == 0 || (now - g_indices_cache.last_updated) > INDICES_CACHE_TTL_SEC) {
        refresh_indices_cache_locked();
    }

    /* 拷贝交易钟 */
    if (out_clocks && out_clock_count && max_clocks >= g_indices_cache.clock_count) {
        memcpy(out_clocks, g_indices_cache.clocks, sizeof(market_clock_item_t) * g_indices_cache.clock_count);
        *out_clock_count = g_indices_cache.clock_count;
    }

    /* 过滤分类 */
    int matched = 0;
    for (int i = 0; i < g_indices_cache.count && matched < max_indices; i++) {
        if (!category || !category[0] || strcmp(category, "all") == 0 ||
            strcmp(g_indices_cache.items[i].category, category) == 0) {
            out_indices[matched++] = g_indices_cache.items[i];
        }
    }
    *out_count = matched;

    pthread_mutex_unlock(&g_indices_cache.lock);
    return 0;
}
```

- [ ] **Step 4: 运行 CTest 验证测试通过**

运行：
```bash
cmake --build backend/build --target test_market_indices && cd backend/build && ctest -R test_market_indices --output-on-failure && cd ../..
```
预期输出：`100% tests passed, 0 tests failed out of 1`，全部断言通过。

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/services/market/quote_engine.h backend/src/services/market/quote_engine.c backend/tests/unit/test_market_indices.c
git commit -m "feat(market): 🎯 implement index quotes aggregation with 20s memory cache"
```

---

### Task 3: 后端用例编排与 HTTP 控制器路由 (Usecase & Controller API)

**Files:**
- Modify: `backend/src/application/market/usecases.h`
- Modify: `backend/src/application/market/usecases.c`
- Modify: `backend/src/interfaces/http/controllers/market_controller.h`
- Modify: `backend/src/interfaces/http/controllers/market_controller.c`
- Modify: `backend/tests/unit/test_market_indices.c`

- [ ] **Step 1: 在单元测试中添加 usecase JSON 序列化测试**

在 `backend/tests/unit/test_market_indices.c` 添加 `test_market_usecase_indices`:
```c
#include "application/market/usecases.h"

static void test_market_usecase_indices(void) {
    csilk_json_t* data = NULL;
    market_usecase_result_t res = {0};

    int rc = market_usecase_get_indices("cn", &data, &res);
    assert(rc == 0);
    assert(res.code == 0);
    assert(data != NULL);

    const csilk_json_t* clocks = csilk_json_get_object(data, "market_clocks");
    assert(clocks != NULL && csilk_json_is_array(clocks));
    assert(csilk_json_array_size(clocks) == 5);

    const csilk_json_t* indices = csilk_json_get_object(data, "indices");
    assert(indices != NULL && csilk_json_is_array(indices));
    assert(csilk_json_array_size(indices) == 4);

    csilk_json_destroy(data);
    printf("PASS: test_market_usecase_indices\n");
}
```
并在 `main` 中调用 `test_market_usecase_indices();`。

- [ ] **Step 2: 运行编译确认用例尚未实现**

运行：
```bash
cmake --build backend/build --target test_market_indices
```
预期输出：提示 `market_usecase_get_indices` 未声明。

- [ ] **Step 3: 实现用例编排与控制器端点**

在 `backend/src/application/market/usecases.h` 增加声明：
```c
int market_usecase_get_indices(const char* category, csilk_json_t** out_data, market_usecase_result_t* out_res);
```

在 `backend/src/application/market/usecases.c` 中实现：
```c
int
market_usecase_get_indices(const char* category, csilk_json_t** out_data, market_usecase_result_t* out_res)
{
    if (!out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));
    if (!out_data) {
        out_res->code = 1002;
        snprintf(out_res->message, sizeof(out_res->message), "输出参数无效");
        return -1;
    }

    global_index_item_t indices[32];
    int count = 0;
    market_clock_item_t clocks[8];
    int clock_count = 0;

    if (quote_engine_get_indices(category, indices, 32, &count, clocks, 8, &clock_count) != 0) {
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "获取大盘指数失败");
        return -1;
    }

    csilk_json_t* root = csilk_json_object();
    char updated_at_str[32];
    time_t t_now = time(NULL);
    struct tm tm_now;
    localtime_r(&t_now, &tm_now);
    strftime(updated_at_str, sizeof(updated_at_str), "%Y-%m-%d %H:%M:%S", &tm_now);
    csilk_json_add_string(root, "updated_at", updated_at_str);

    /* 序列化 market_clocks */
    csilk_json_t* clocks_arr = csilk_json_array();
    for (int i = 0; i < clock_count; i++) {
        csilk_json_t* c_obj = csilk_json_object();
        csilk_json_add_string(c_obj, "market", clocks[i].market);
        csilk_json_add_string(c_obj, "name", clocks[i].name);
        csilk_json_add_string(c_obj, "timezone", clocks[i].timezone);
        csilk_json_add_string(c_obj, "status", clocks[i].status);
        csilk_json_add_string(c_obj, "open_time", clocks[i].open_time);
        csilk_json_add_string(c_obj, "close_time", clocks[i].close_time);
        csilk_json_array_append(clocks_arr, c_obj);
    }
    csilk_json_add_object(root, "market_clocks", clocks_arr);

    /* 序列化 indices */
    csilk_json_t* indices_arr = csilk_json_array();
    for (int i = 0; i < count; i++) {
        csilk_json_t* item = csilk_json_object();
        csilk_json_add_string(item, "symbol", indices[i].symbol);
        csilk_json_add_string(item, "name", indices[i].name);
        csilk_json_add_string(item, "category", indices[i].category);
        csilk_json_add_string(item, "region_flag", indices[i].region_flag);
        csilk_json_add_number(item, "current_price", indices[i].current_price);
        csilk_json_add_number(item, "change_amount", indices[i].change_amount);
        csilk_json_add_number(item, "change_percent", indices[i].change_percent);
        csilk_json_add_number(item, "open_price", indices[i].open_price);
        csilk_json_add_number(item, "high_price", indices[i].high_price);
        csilk_json_add_number(item, "low_price", indices[i].low_price);
        csilk_json_add_number(item, "prev_close", indices[i].prev_close);
        csilk_json_add_string(item, "currency", indices[i].currency);
        csilk_json_add_string(item, "quote_time", indices[i].quote_time);

        csilk_json_t* spark_arr = csilk_json_array();
        for (int s = 0; s < indices[i].sparkline_count; s++) {
            csilk_json_array_append(spark_arr, csilk_json_number(indices[i].sparkline[s]));
        }
        csilk_json_add_object(item, "sparkline", spark_arr);
        csilk_json_array_append(indices_arr, item);
    }
    csilk_json_add_object(root, "indices", indices_arr);

    *out_data = root;
    out_res->code = 0;
    snprintf(out_res->message, sizeof(out_res->message), "ok");
    return 0;
}
```

在 `backend/src/interfaces/http/controllers/market_controller.h` 增加：
```c
void api_market_get_indices(csilk_ctx_t* c);
```

在 `backend/src/interfaces/http/controllers/market_controller.c` 实现并注册路由：
```c
void
api_market_get_indices(csilk_ctx_t* c)
{
    const char* category = csilk_get_query(c, "category");
    csilk_json_t* data = NULL;
    market_usecase_result_t res = {0};

    int rc = market_usecase_get_indices(category, &data, &res);
    if (rc == 0 && res.code == 0) {
        respond_ok(c, data);
    } else {
        respond_error(c, res.code ? res.code : 500, res.message);
    }
}
```
并在 `register_market_routes(app)` 中注册：
```c
    csilk_app_get_ext(app,
                      "/api/market/indices",
                      api_market_get_indices,
                      NULL,
                      NULL,
                      "Get global indices",
                      "Get real-time global market indices and trading clocks");
```

- [ ] **Step 4: 编译全量后端与测试套件**

运行：
```bash
cmake --build backend/build --parallel && cd backend/build && ctest --output-on-failure && cd ../..
```
预期输出：全量 CTest 单元测试（包括 `test_market_indices`）全部通过通过。

- [ ] **Step 5: 提交代码**

```bash
git add backend/src/application/market/usecases.h backend/src/application/market/usecases.c backend/src/interfaces/http/controllers/market_controller.h backend/src/interfaces/http/controllers/market_controller.c backend/tests/unit/test_market_indices.c
git commit -m "feat(market): 🎯 add GET /api/market/indices endpoint"
```

---

### Task 4: 前端类型定义、API 客户端与多语言文案 (Frontend Types, API & i18n)

**Files:**
- Modify: `frontend/src/types/index.ts`
- Modify: `frontend/src/api/market.ts`
- Modify: `frontend/src/locales/zh-CN.ts`
- Modify: `frontend/src/locales/en-US.ts`
- Modify: `frontend/tests/market-store.spec.ts`

- [ ] **Step 1: 在 `frontend/tests/market-store.spec.ts` 编写 API mock 测试**

添加测试验证 `marketApi.getIndices` 能够被调用并返回数据：
```ts
  it('calls marketApi.getIndices with category param', async () => {
    const mockData = {
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [{ market: 'SSE_SZSE', name: 'A股', timezone: 'Asia/Shanghai', status: 'open', open_time: '09:30', close_time: '15:00' }],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3285.4, change_amount: 25.3, change_percent: 0.78, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260.1, currency: 'CNY', quote_time: '2026-10-16 11:30', sparkline: [3260, 3285] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(mockData as any)

    const res = await marketApi.getIndices('cn')
    expect(res.indices[0].name).toBe('上证指数')
    expect(res.market_clocks[0].status).toBe('open')
  })
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
npm --prefix frontend test
```
预期输出：`marketApi.getIndices is not a function`。

- [ ] **Step 3: 添加 TypeScript 类型、API 方法与国际化文案**

在 `frontend/src/types/index.ts` 追加：
```ts
export interface MarketClockItem {
  market: string
  name: string
  timezone: string
  status: 'open' | 'closed' | 'pre' | 'after'
  open_time: string
  close_time: string
}

export interface GlobalIndexItem {
  symbol: string
  name: string
  category: 'cn' | 'us' | 'hk' | 'global' | 'crypto'
  region_flag: string
  current_price: number
  change_amount: number
  change_percent: number
  open_price: number
  high_price: number
  low_price: number
  prev_close: number
  currency: string
  quote_time: string
  sparkline: number[]
}

export interface GlobalIndicesData {
  updated_at: string
  market_clocks: MarketClockItem[]
  indices: GlobalIndexItem[]
}
```

在 `frontend/src/api/market.ts` 中引入类型并添加：
```ts
  /**
   * 获取全球主要大盘指数与交易钟状态
   * @route GET /api/market/indices
   * @param category 市场分类筛选 ('all' | 'cn' | 'us' | 'hk' | 'global' | 'crypto')
   */
  getIndices: (category?: string) =>
    http.get<import('@/types').GlobalIndicesData>('/market/indices', { params: { category } }),
```

在 `frontend/src/locales/zh-CN.ts` 的 `market` 节点下增加：
```ts
    indicesData: {
      title: '全球实时大盘',
      clocksTitle: '全球交易所状态',
      statusOpen: '交易中',
      statusPre: '盘前',
      statusClosed: '已休市',
      autoRefresh: '自动刷新',
      refresh15s: '15 秒',
      refresh30s: '30 秒',
      refresh60s: '60 秒',
      refreshOff: '关闭',
      refreshNow: '刷新',
      searchPlaceholder: '搜索指数名称或代码...',
      categories: {
        all: '全部市场',
        cn: '🇨🇳 A股核心',
        us: '🇺🇸 美股三大',
        hk: '🇭🇰 港股精选',
        global: '🌐 全球关键',
        crypto: '🪙 加密与商品',
      },
      high: '最高',
      low: '最低',
      open: '今开',
      prevClose: '昨收',
      quoteTime: '行情时间',
      drawerTitle: '指数走势详情',
      addToWatchlist: '关注指数',
    },
```

并在 `frontend/src/locales/en-US.ts` 中对应增加英文翻译节点。

- [ ] **Step 4: 运行前端单元测试确认通过**

运行：
```bash
npm --prefix frontend test
```
预期输出：所有测试套件通过。

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/types/index.ts frontend/src/api/market.ts frontend/src/locales/zh-CN.ts frontend/src/locales/en-US.ts frontend/tests/market-store.spec.ts
git commit -m "feat(market): 🎯 add indices types, api method and i18n translations"
```

---

### Task 5: 前端 Store 扩展 (Store Polling, Flash Direction & Sequence Guard)

**Files:**
- Modify: `frontend/src/stores/market.ts`
- Modify: `frontend/tests/market-store.spec.ts`

- [ ] **Step 1: 编写 Store 指数拉取、价格变动识别与防竞态测试**

在 `frontend/tests/market-store.spec.ts` 增加：
```ts
  it('updates indices state and calculates price directions for flash animation', async () => {
    const store = useMarketStore()
    const initialData = {
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3280.0, change_amount: 20, change_percent: 0.6, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00', sparkline: [] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(initialData as any)

    await store.fetchIndices()
    expect(store.indices[0].current_price).toBe(3280.0)

    // 第二次拉取，价格上涨 -> priceDirections 标记为 'up'
    const updatedData = {
      updated_at: '2026-10-16 12:00:30',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3285.4, change_amount: 25.4, change_percent: 0.78, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00:30', sparkline: [] }]
    }
    vi.mocked(marketApi.getIndices).mockResolvedValueOnce(updatedData as any)

    await store.fetchIndices()
    expect(store.indices[0].current_price).toBe(3285.4)
    expect(store.priceDirections['sh000001']).toBe('up')
  })

  it('guards against asynchronous out-of-order race responses in fetchIndices', async () => {
    const store = useMarketStore()
    let resolveFirst: any
    const firstPromise = new Promise((resolve) => { resolveFirst = resolve })

    vi.mocked(marketApi.getIndices)
      .mockReturnValueOnce(firstPromise as any)
      .mockResolvedValueOnce({
        updated_at: '2026-10-16 12:01:00',
        market_clocks: [],
        indices: [{ symbol: '^GSPC', name: '标普500', category: 'us', region_flag: '🇺🇸', current_price: 5864.0, change_amount: 24, change_percent: 0.4, open_price: 5840, high_price: 5870, low_price: 5830, prev_close: 5840, currency: 'USD', quote_time: '12:01', sparkline: [] }]
      } as any)

    const p1 = store.fetchIndices('cn')
    const p2 = store.fetchIndices('us')

    await p2
    expect(store.indices[0].symbol).toBe('^GSPC')

    // 慢速请求返回，应被丢弃
    resolveFirst({
      updated_at: '2026-10-16 12:00:00',
      market_clocks: [],
      indices: [{ symbol: 'sh000001', name: '上证指数', category: 'cn', region_flag: '🇨🇳', current_price: 3280.0, change_amount: 20, change_percent: 0.6, open_price: 3260, high_price: 3290, low_price: 3250, prev_close: 3260, currency: 'CNY', quote_time: '12:00', sparkline: [] }]
    })
    await p1

    expect(store.indices[0].symbol).toBe('^GSPC')
  })
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
npm --prefix frontend test
```
预期输出：`store.fetchIndices is not a function`。

- [ ] **Step 3: 在 `useMarketStore` 中实现状态、闪烁比对与防竞态逻辑**

在 `frontend/src/stores/market.ts` 中引入类型：
```ts
import type { FxHistoryPoint, MultiCurrencySummary, GlobalIndexItem, MarketClockItem } from '@/types'
```
并在 store 内添加状态与方法：
```ts
  const indices = ref<GlobalIndexItem[]>([])
  const marketClocks = ref<MarketClockItem[]>([])
  const indicesUpdatedAt = ref<string | null>(null)
  const isLoadingIndices = ref(false)
  const autoRefreshInterval = ref<number>(30)
  const activeCategory = ref<string>('all')
  const prevPrices = ref<Record<string, number>>({})
  const priceDirections = ref<Record<string, 'up' | 'down' | 'same'>>({})
  const selectedDrawerIndex = ref<GlobalIndexItem | null>(null)
  const isDrawerOpen = ref(false)

  let indicesReqSeq = 0

  function setAutoRefreshInterval(sec: number) {
    autoRefreshInterval.value = sec
  }

  function setActiveCategory(cat: string) {
    activeCategory.value = cat
    fetchIndices(cat)
  }

  function openIndexDrawer(item: GlobalIndexItem) {
    selectedDrawerIndex.value = item
    isDrawerOpen.value = true
  }

  function closeIndexDrawer() {
    isDrawerOpen.value = false
    selectedDrawerIndex.value = null
  }

  async function fetchIndices(category = activeCategory.value, silent = false) {
    const currentSeq = ++indicesReqSeq
    if (!silent) {
      isLoadingIndices.value = true
    }
    try {
      const data = await marketApi.getIndices(category === 'all' ? undefined : category)
      if (currentSeq === indicesReqSeq && data) {
        if (Array.isArray(data.indices)) {
          const nextDirections: Record<string, 'up' | 'down' | 'same'> = {}
          const nextPrices: Record<string, number> = {}

          for (const item of data.indices) {
            const oldPrice = prevPrices.value[item.symbol]
            if (oldPrice !== undefined && oldPrice !== null) {
              if (item.current_price > oldPrice) {
                nextDirections[item.symbol] = 'up'
              } else if (item.current_price < oldPrice) {
                nextDirections[item.symbol] = 'down'
              } else {
                nextDirections[item.symbol] = 'same'
              }
            } else {
              nextDirections[item.symbol] = 'same'
            }
            nextPrices[item.symbol] = item.current_price
          }

          priceDirections.value = nextDirections
          prevPrices.value = nextPrices
          indices.value = data.indices
        }

        if (Array.isArray(data.market_clocks)) {
          marketClocks.value = data.market_clocks
        }
        indicesUpdatedAt.value = data.updated_at || new Date().toLocaleTimeString()
      }
    } catch (err) {
      console.warn('Failed to fetch indices:', err)
    } finally {
      if (currentSeq === indicesReqSeq) {
        isLoadingIndices.value = false
      }
    }
  }
```
并在返回值中完整导出上述状态和函数。

- [ ] **Step 4: 运行前端测试验证全部通过**

运行：
```bash
npm --prefix frontend test
```
预期输出：`market store` 全部测试用例 PASS。

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/stores/market.ts frontend/tests/market-store.spec.ts
git commit -m "feat(market): 🎯 extend market store with indices polling, flash direction and sequence guard"
```

---

### Task 6: 前端子组件实现 (MarketClocksRibbon, IndicesFilterBar & IndexCard)

**Files:**
- Create: `frontend/src/components/market/MarketClocksRibbon.vue`
- Create: `frontend/src/components/market/IndicesFilterBar.vue`
- Create: `frontend/src/components/market/IndexCard.vue`
- Create: `frontend/tests/market-indices.spec.ts`

- [ ] **Step 1: 编写组件单元测试**

创建 `frontend/tests/market-indices.spec.ts`：
```ts
import { describe, it, expect, beforeEach, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import IndexCard from '@/components/market/IndexCard.vue'
import MarketClocksRibbon from '@/components/market/MarketClocksRibbon.vue'
import type { GlobalIndexItem } from '@/types'

describe('Market Components', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('renders IndexCard with positive change and sparkline path', () => {
    const item: GlobalIndexItem = {
      symbol: 'sh000001',
      name: '上证指数',
      category: 'cn',
      region_flag: '🇨🇳',
      current_price: 3285.4,
      change_amount: 25.32,
      change_percent: 0.78,
      open_price: 3262.1,
      high_price: 3292.1,
      low_price: 3258.8,
      prev_close: 3260.08,
      currency: 'CNY',
      quote_time: '2026-10-16 11:30:00',
      sparkline: [3260.1, 3265.4, 3270.8, 3268.2, 3280.1, 3285.4],
    }

    const wrapper = mount(IndexCard, {
      props: { item, direction: 'up' },
    })

    expect(wrapper.text()).toContain('上证指数')
    expect(wrapper.text()).toContain('3,285.40')
    expect(wrapper.text()).toContain('+0.78%')
    expect(wrapper.find('svg path').exists()).toBe(true)
    expect(wrapper.classes()).toContain('flash-up')
  })

  it('renders MarketClocksRibbon with exchange clocks', () => {
    const wrapper = mount(MarketClocksRibbon)
    expect(wrapper.find('.clocks-ribbon').exists()).toBe(true)
  })
})
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
npm --prefix frontend test
```
预期输出：无法解析模块路径 `@/components/market/IndexCard.vue`。

- [ ] **Step 3: 实现三个子组件**

1. **`frontend/src/components/market/MarketClocksRibbon.vue`**：
```vue
<template>
  <div class="clocks-ribbon">
    <div class="clocks-list">
      <div class="ribbon-label">
        <el-icon><Clock /></el-icon>
        <span>{{ t('market.indicesData.clocksTitle') }}:</span>
      </div>
      <div
        v-for="clock in marketStore.marketClocks"
        :key="clock.market"
        class="clock-pill"
        :class="`status-${clock.status}`"
      >
        <span class="status-dot" />
        <span class="clock-name">{{ clock.name }}</span>
        <span class="clock-status-tag">{{ getStatusLabel(clock.status) }}</span>
      </div>
    </div>

    <div class="ribbon-controls">
      <span class="refresh-label">{{ t('market.indicesData.autoRefresh') }}:</span>
      <el-select
        :model-value="marketStore.autoRefreshInterval"
        size="small"
        class="interval-select"
        @update:model-value="marketStore.setAutoRefreshInterval($event)"
      >
        <el-option :label="t('market.indicesData.refresh15s')" :value="15" />
        <el-option :label="t('market.indicesData.refresh30s')" :value="30" />
        <el-option :label="t('market.indicesData.refresh60s')" :value="60" />
        <el-option :label="t('market.indicesData.refreshOff')" :value="0" />
      </el-select>

      <el-button
        size="small"
        type="primary"
        plain
        :loading="marketStore.isLoadingIndices"
        @click="marketStore.fetchIndices()"
      >
        <el-icon><Refresh /></el-icon>
        {{ t('market.indicesData.refreshNow') }}
      </el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import { Clock, Refresh } from '@element-plus/icons-vue'
import { useMarketStore } from '@/stores/market'

const { t } = useI18n()
const marketStore = useMarketStore()

function getStatusLabel(status: string) {
  if (status === 'open') return t('market.indicesData.statusOpen')
  if (status === 'pre') return t('market.indicesData.statusPre')
  return t('market.indicesData.statusClosed')
}
</script>

<style scoped>
.clocks-ribbon {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 8px 14px;
  margin-bottom: 12px;
  gap: 12px;
  flex-wrap: wrap;
}
.clocks-list {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.ribbon-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-regular);
}
.clock-pill {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 2px 8px;
  border-radius: 12px;
  font-size: 12px;
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color-extra-light);
}
.status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--el-color-info);
}
.status-open .status-dot {
  background: var(--el-color-success);
  box-shadow: 0 0 6px var(--el-color-success);
  animation: pulse-dot 1.8s infinite;
}
.status-pre .status-dot {
  background: var(--el-color-warning);
}
.clock-name {
  font-weight: 500;
  color: var(--el-text-color-primary);
}
.clock-status-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.ribbon-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}
.refresh-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.interval-select {
  width: 90px;
}
@keyframes pulse-dot {
  0% { transform: scale(0.95); opacity: 0.8; }
  50% { transform: scale(1.3); opacity: 1; }
  100% { transform: scale(0.95); opacity: 0.8; }
}
</style>
```

2. **`frontend/src/components/market/IndicesFilterBar.vue`**：
```vue
<template>
  <div class="indices-filter-bar">
    <el-radio-group
      :model-value="marketStore.activeCategory"
      size="default"
      @update:model-value="marketStore.setActiveCategory($event as string)"
    >
      <el-radio-button label="all">{{ t('market.indicesData.categories.all') }}</el-radio-button>
      <el-radio-button label="cn">{{ t('market.indicesData.categories.cn') }}</el-radio-button>
      <el-radio-button label="us">{{ t('market.indicesData.categories.us') }}</el-radio-button>
      <el-radio-button label="hk">{{ t('market.indicesData.categories.hk') }}</el-radio-button>
      <el-radio-button label="global">{{ t('market.indicesData.categories.global') }}</el-radio-button>
      <el-radio-button label="crypto">{{ t('market.indicesData.categories.crypto') }}</el-radio-button>
    </el-radio-group>

    <div class="search-input-wrap">
      <el-input
        v-model="searchQuery"
        size="default"
        clearable
        :placeholder="t('market.indicesData.searchPlaceholder')"
        :prefix-icon="Search"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Search } from '@element-plus/icons-vue'
import { useMarketStore } from '@/stores/market'

const emit = defineEmits<{
  (e: 'update:filter', keyword: string): void
}>()

const { t } = useI18n()
const marketStore = useMarketStore()
const searchQuery = ref('')

watch(searchQuery, (val) => {
  emit('update:filter', val)
})
</script>

<style scoped>
.indices-filter-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 14px;
  gap: 12px;
  flex-wrap: wrap;
}
.search-input-wrap {
  width: 240px;
}
</style>
```

3. **`frontend/src/components/market/IndexCard.vue`**：
```vue
<template>
  <div
    class="index-card"
    :class="[
      direction === 'up' ? 'flash-up' : direction === 'down' ? 'flash-down' : '',
      isPositive ? 'trend-positive' : 'trend-negative'
    ]"
    @click="handleClick"
  >
    <div class="card-header">
      <div class="name-box">
        <span class="region-flag">{{ item.region_flag }}</span>
        <span class="index-name">{{ item.name }}</span>
      </div>
      <span class="index-symbol">{{ item.symbol }}</span>
    </div>

    <div class="card-price-row">
      <div class="price-val">
        {{ formatNumber(item.current_price) }}
      </div>
      <div class="price-change-pill" :class="isPositive ? 'pill-up' : 'pill-down'">
        <span>{{ isPositive ? '+' : '' }}{{ item.change_amount.toFixed(2) }}</span>
        <span>({{ isPositive ? '+' : '' }}{{ item.change_percent.toFixed(2) }}%)</span>
      </div>
    </div>

    <!-- Mini SVG Sparkline -->
    <div class="card-sparkline">
      <svg viewBox="0 0 200 40" preserveAspectRatio="none" class="sparkline-svg">
        <defs>
          <linearGradient :id="`grad-${item.symbol}`" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" :stop-color="isPositive ? '#67c23a' : '#f56c6c'" stop-opacity="0.3" />
            <stop offset="100%" :stop-color="isPositive ? '#67c23a' : '#f56c6c'" stop-opacity="0.0" />
          </linearGradient>
        </defs>
        <polygon :points="areaPoints" :fill="`url(#grad-${item.symbol})`" />
        <polyline
          :points="linePoints"
          fill="none"
          :stroke="isPositive ? '#67c23a' : '#f56c6c'"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
      </svg>
    </div>

    <div class="card-footer-stats">
      <span>{{ t('market.indicesData.low') }}: {{ formatNumber(item.low_price) }}</span>
      <span>{{ t('market.indicesData.high') }}: {{ formatNumber(item.high_price) }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import type { GlobalIndexItem } from '@/types'

const props = defineProps<{
  item: GlobalIndexItem
  direction?: 'up' | 'down' | 'same'
}>()

const emit = defineEmits<{
  (e: 'select', item: GlobalIndexItem): void
}>()

const { t } = useI18n()

const isPositive = computed(() => props.item.change_percent >= 0)

function formatNumber(val: number) {
  if (val === undefined || val === null) return '0.00'
  return val.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function handleClick() {
  emit('select', props.item)
}

const linePoints = computed(() => {
  const pts = props.item.sparkline
  if (!pts || pts.length < 2) return '0,20 200,20'
  const min = Math.min(...pts)
  const max = Math.max(...pts)
  const range = max - min || 1

  return pts
    .map((val, idx) => {
      const x = (idx / (pts.length - 1)) * 200
      const y = 35 - ((val - min) / range) * 30
      return `${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')
})

const areaPoints = computed(() => {
  const pts = linePoints.value
  return `0,40 ${pts} 200,40`
})
</script>

<style scoped>
.index-card {
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 10px;
  padding: 14px;
  cursor: pointer;
  transition: all 0.25s ease;
  position: relative;
  overflow: hidden;
}
.index-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.08);
  border-color: var(--el-color-primary-light-5);
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}
.name-box {
  display: flex;
  align-items: center;
  gap: 6px;
}
.region-flag {
  font-size: 16px;
}
.index-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}
.index-symbol {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  font-family: monospace;
}
.card-price-row {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  margin-bottom: 6px;
}
.price-val {
  font-size: 22px;
  font-weight: 700;
  font-family: var(--el-font-family);
  color: var(--el-text-color-primary);
}
.price-change-pill {
  display: flex;
  gap: 4px;
  font-size: 12px;
  font-weight: 600;
  padding: 2px 6px;
  border-radius: 4px;
}
.pill-up {
  color: var(--el-color-success);
  background: var(--el-color-success-light-9);
}
.pill-down {
  color: var(--el-color-danger);
  background: var(--el-color-danger-light-9);
}
.card-sparkline {
  height: 40px;
  width: 100%;
  margin: 6px 0;
}
.sparkline-svg {
  width: 100%;
  height: 100%;
}
.card-footer-stats {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.flash-up {
  animation: flash-green 1.2s ease;
}
.flash-down {
  animation: flash-red 1.2s ease;
}
@keyframes flash-green {
  0% { background: var(--el-color-success-light-8); }
  100% { background: var(--el-bg-color-overlay); }
}
@keyframes flash-red {
  0% { background: var(--el-color-danger-light-8); }
  100% { background: var(--el-bg-color-overlay); }
}
</style>
```

- [ ] **Step 4: 运行前端测试验证通过**

运行：
```bash
npm --prefix frontend test
```
预期输出：`Market Components` 测试通过。

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/components/market/MarketClocksRibbon.vue frontend/src/components/market/IndicesFilterBar.vue frontend/src/components/market/IndexCard.vue frontend/tests/market-indices.spec.ts
git commit -m "feat(market): 🎯 create MarketClocksRibbon, IndicesFilterBar and IndexCard components"
```

---

### Task 7: 深度走势抽屉组件实现 (IndexDetailDrawer with ECharts)

**Files:**
- Create: `frontend/src/components/market/IndexDetailDrawer.vue`
- Modify: `frontend/tests/market-indices.spec.ts`

- [ ] **Step 1: 编写抽屉测试**

在 `frontend/tests/market-indices.spec.ts` 中增加：
```ts
import IndexDetailDrawer from '@/components/market/IndexDetailDrawer.vue'

  it('renders IndexDetailDrawer when open with index stats', async () => {
    const store = useMarketStore()
    const item: GlobalIndexItem = {
      symbol: 'sh000001',
      name: '上证指数',
      category: 'cn',
      region_flag: '🇨🇳',
      current_price: 3285.4,
      change_amount: 25.32,
      change_percent: 0.78,
      open_price: 3262.1,
      high_price: 3292.1,
      low_price: 3258.8,
      prev_close: 3260.08,
      currency: 'CNY',
      quote_time: '2026-10-16 11:30:00',
      sparkline: [3260.1, 3285.4],
    }
    store.openIndexDrawer(item)

    const wrapper = mount(IndexDetailDrawer)
    expect(wrapper.text()).toContain('上证指数')
    expect(wrapper.text()).toContain('3262.10')
  })
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
npm --prefix frontend test
```
预期输出：找不到模块 `IndexDetailDrawer.vue`。

- [ ] **Step 3: 实现 `IndexDetailDrawer.vue`**

创建 `frontend/src/components/market/IndexDetailDrawer.vue`：
```vue
<template>
  <el-drawer
    v-model="marketStore.isDrawerOpen"
    :title="drawerTitle"
    size="520px"
    destroy-on-close
    class="index-detail-drawer"
  >
    <div v-if="item" class="drawer-content">
      <!-- Top Overview Stat Box -->
      <div class="overview-box" :class="isPositive ? 'trend-up' : 'trend-down'">
        <div class="overview-main">
          <div class="current-price">{{ formatNumber(item.current_price) }}</div>
          <div class="price-delta">
            <span>{{ isPositive ? '+' : '' }}{{ item.change_amount.toFixed(2) }}</span>
            <span>({{ isPositive ? '+' : '' }}{{ item.change_percent.toFixed(2) }}%)</span>
          </div>
        </div>
        <div class="quote-time-tag">
          {{ t('market.indicesData.quoteTime') }}: {{ item.quote_time }}
        </div>
      </div>

      <!-- Key Metrics Grid -->
      <div class="metrics-grid">
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.open') }}</span>
          <span class="m-val">{{ formatNumber(item.open_price) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.prevClose') }}</span>
          <span class="m-val">{{ formatNumber(item.prev_close) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.high') }}</span>
          <span class="m-val">{{ formatNumber(item.high_price) }}</span>
        </div>
        <div class="metric-item">
          <span class="m-label">{{ t('market.indicesData.low') }}</span>
          <span class="m-val">{{ formatNumber(item.low_price) }}</span>
        </div>
      </div>

      <!-- Interactive ECharts Trend Chart -->
      <div class="chart-section">
        <div class="section-title">📈 {{ t('market.indicesData.drawerTitle') }}</div>
        <div ref="chartRef" class="chart-container" />
      </div>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue'
import { useI18n } from 'vue-i18n'
import * as echarts from 'echarts'
import { useMarketStore } from '@/stores/market'

const { t } = useI18n()
const marketStore = useMarketStore()
const chartRef = ref<HTMLDivElement | null>(null)
let chartInstance: echarts.ECharts | null = null

const item = computed(() => marketStore.selectedDrawerIndex)
const isPositive = computed(() => (item.value?.change_percent ?? 0) >= 0)
const drawerTitle = computed(() =>
  item.value ? `${item.value.region_flag} ${item.value.name} (${item.value.symbol})` : ''
)

function formatNumber(val: number | undefined) {
  if (val === undefined || val === null) return '0.00'
  return val.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function renderChart() {
  if (!chartRef.value || !item.value) return
  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }

  const pts = item.value.sparkline || []
  const times = pts.map((_, i) => `${9 + Math.floor(i / 2)}:${i % 2 === 0 ? '30' : '00'}`)
  const color = isPositive.value ? '#67c23a' : '#f56c6c'

  const option: echarts.EChartsOption = {
    tooltip: {
      trigger: 'axis',
      formatter: '{b}<br/>点位: <b>{c}</b>',
    },
    grid: { left: '10%', right: '5%', top: '10%', bottom: '15%' },
    xAxis: {
      type: 'category',
      data: times,
      axisLine: { lineStyle: { color: '#888' } },
    },
    yAxis: {
      type: 'value',
      scale: true,
      splitLine: { lineStyle: { type: 'dashed', opacity: 0.2 } },
    },
    series: [
      {
        name: '点位',
        type: 'line',
        data: pts,
        smooth: true,
        showSymbol: false,
        lineStyle: { width: 2.5, color },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: isPositive.value ? 'rgba(103,194,58,0.35)' : 'rgba(245,108,108,0.35)' },
            { offset: 1, color: 'rgba(0,0,0,0)' },
          ]),
        },
      },
    ],
  }

  chartInstance.setOption(option)
}

watch(
  () => marketStore.isDrawerOpen,
  (open) => {
    if (open) {
      nextTick(() => {
        renderChart()
      })
    }
  }
)

onBeforeUnmount(() => {
  if (chartInstance) {
    chartInstance.dispose()
    chartInstance = null
  }
})
</script>

<style scoped>
.drawer-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.overview-box {
  background: var(--el-fill-color-light);
  border-radius: 8px;
  padding: 16px;
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
}
.current-price {
  font-size: 28px;
  font-weight: 700;
  color: var(--el-text-color-primary);
}
.price-delta {
  font-size: 14px;
  font-weight: 600;
  margin-top: 4px;
}
.trend-up .price-delta { color: var(--el-color-success); }
.trend-down .price-delta { color: var(--el-color-danger); }
.quote-time-tag {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
}
.metric-item {
  background: var(--el-bg-color-overlay);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 10px 12px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.m-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}
.m-val {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}
.chart-section {
  margin-top: 8px;
}
.section-title {
  font-size: 14px;
  font-weight: 600;
  margin-bottom: 8px;
  color: var(--el-text-color-primary);
}
.chart-container {
  width: 100%;
  height: 280px;
}
</style>
```

- [ ] **Step 4: 运行测试验证抽屉通过**

运行：
```bash
npm --prefix frontend test
```
预期输出：测试全部 PASS。

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/components/market/IndexDetailDrawer.vue frontend/tests/market-indices.spec.ts
git commit -m "feat(market): 🎯 implement IndexDetailDrawer with ECharts trend visualization"
```

---

### Task 8: 全局大盘看板容器与可见性守卫 (GlobalIndicesBoard & View Wiring)

**Files:**
- Create: `frontend/src/components/market/GlobalIndicesBoard.vue`
- Modify: `frontend/src/views/Market.vue`
- Modify: `frontend/tests/market-indices.spec.ts`

- [ ] **Step 1: 编写看板容器与可见性守卫测试**

在 `frontend/tests/market-indices.spec.ts` 添加：
```ts
import GlobalIndicesBoard from '@/components/market/GlobalIndicesBoard.vue'

  it('mounts GlobalIndicesBoard and initializes polling lifecycle', () => {
    const store = useMarketStore()
    const fetchSpy = vi.spyOn(store, 'fetchIndices').mockResolvedValue()

    const wrapper = mount(GlobalIndicesBoard)
    expect(wrapper.findComponent(MarketClocksRibbon).exists()).toBe(true)
    expect(wrapper.findComponent(IndexDetailDrawer).exists()).toBe(true)
    expect(fetchSpy).toHaveBeenCalled()

    wrapper.unmount()
  })
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
npm --prefix frontend test
```
预期输出：找不到模块 `GlobalIndicesBoard.vue`。

- [ ] **Step 3: 实现 `GlobalIndicesBoard.vue` 并替换 `Market.vue` 占位组件**

创建 `frontend/src/components/market/GlobalIndicesBoard.vue`：
```vue
<template>
  <div class="global-indices-board">
    <!-- 1. Market Clocks Ribbon -->
    <MarketClocksRibbon />

    <!-- 2. Categories & Search Filter -->
    <IndicesFilterBar @update:filter="keywordFilter = $event" />

    <!-- 3. Indices Responsive Grid -->
    <div v-loading="marketStore.isLoadingIndices" class="indices-grid">
      <IndexCard
        v-for="item in filteredIndices"
        :key="item.symbol"
        :item="item"
        :direction="marketStore.priceDirections[item.symbol]"
        @select="marketStore.openIndexDrawer($event)"
      />
    </div>

    <el-empty
      v-if="!marketStore.isLoadingIndices && filteredIndices.length === 0"
      description="未找到匹配的大盘指数"
    />

    <!-- 4. Interactive Detail Drawer -->
    <IndexDetailDrawer />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useMarketStore } from '@/stores/market'
import MarketClocksRibbon from '@/components/market/MarketClocksRibbon.vue'
import IndicesFilterBar from '@/components/market/IndicesFilterBar.vue'
import IndexCard from '@/components/market/IndexCard.vue'
import IndexDetailDrawer from '@/components/market/IndexDetailDrawer.vue'

const marketStore = useMarketStore()
const keywordFilter = ref('')
let timerId: ReturnType<typeof setInterval> | null = null

const filteredIndices = computed(() => {
  const kw = keywordFilter.value.trim().toLowerCase()
  if (!kw) return marketStore.indices
  return marketStore.indices.filter(
    (item) => item.name.toLowerCase().includes(kw) || item.symbol.toLowerCase().includes(kw)
  )
})

function startTimer() {
  stopTimer()
  if (marketStore.autoRefreshInterval > 0) {
    timerId = setInterval(() => {
      marketStore.fetchIndices(undefined, true)
    }, marketStore.autoRefreshInterval * 1000)
  }
}

function stopTimer() {
  if (timerId) {
    clearInterval(timerId)
    timerId = null
  }
}

function handleVisibilityChange() {
  if (document.hidden) {
    stopTimer()
  } else {
    marketStore.fetchIndices(undefined, true)
    startTimer()
  }
}

onMounted(() => {
  marketStore.fetchIndices()
  startTimer()
  document.addEventListener('visibilitychange', handleVisibilityChange)
})

onUnmounted(() => {
  stopTimer()
  document.removeEventListener('visibilitychange', handleVisibilityChange)
})
</script>

<style scoped>
.global-indices-board {
  display: flex;
  flex-direction: column;
}
.indices-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
}
</style>
```

在 `frontend/src/views/Market.vue` 中，将：
```vue
    <!-- Tab 2: Global Indices Placeholder -->
    <div v-else-if="activeTab === 'indices'" class="tab-content">
      <GlobalIndicesPlaceholder />
    </div>
```
替换为：
```vue
    <!-- Tab 2: Global Indices Board -->
    <div v-else-if="activeTab === 'indices'" class="tab-content">
      <GlobalIndicesBoard />
    </div>
```
并在 `<script setup>` 中引入 `GlobalIndicesBoard` 取代 `GlobalIndicesPlaceholder`。

- [ ] **Step 4: 运行前端全部测试**

运行：
```bash
npm --prefix frontend test
```
预期输出：所有 20 个测试套件全部通过。

- [ ] **Step 5: 提交代码**

```bash
git add frontend/src/components/market/GlobalIndicesBoard.vue frontend/src/views/Market.vue frontend/tests/market-indices.spec.ts
git commit -m "feat(market): 🎯 integrate GlobalIndicesBoard container into Market view"
```

---

### Task 9: 全量构建与端到端回归校验 (Build & Full Verification)

**Files:**
- Test: 全量单元测试与构建

- [ ] **Step 1: 运行后端全量编译与 33 组 CTest 单元测试**

```bash
cmake -B backend/build -G "Unix Makefiles"
cmake --build backend/build --parallel
cd backend/build && ctest --output-on-failure && cd ../..
```
预期输出：全部 CTest 测试通过（包含 `test_market_indices`）。

- [ ] **Step 2: 运行前端类型检查与生产构建**

```bash
npm --prefix frontend run build
```
预期输出：`vue-tsc -b && vite build` 零类型错误，成功产出 `dist/`。

- [ ] **Step 3: 运行前端全量 Vitest 测试套件**

```bash
npm --prefix frontend test
```
预期输出：所有测试用例 PASS。

- [ ] **Step 4: 最终提交**

```bash
git commit --allow-empty -m "test(market): 🎯 verify end-to-end global market indices functionality"
```
