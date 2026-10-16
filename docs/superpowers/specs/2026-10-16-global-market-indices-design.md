# 全球大盘实时指数看板架构设计规范 (Global Market Indices Design)

## 1. 概述与背景 (Overview & Background)

在 Minefolio 行情中心（`/market`）的顶层架构规划中，第二标签页（Tab 2: `📈 全球指数大盘`）用于为用户提供全球宏观市场主要大盘指数的实时追踪与行情透视。
此前系统该位置为占位插槽 `GlobalIndicesPlaceholder.vue`。

本设计旨在完整实现全球实时大盘指数看板，满足以下核心目标：
1. **全球核心市场覆盖**：覆盖 A 股核心（上证、深证、创业板、沪深300）、美股三大（标普500、纳指、道琼斯）、港股精选（恒生指数、恒生科技）、全球关键基准（日经225、德国DAX、英国富时100）以及主流加密资产与大宗商品（BTC、ETH、黄金等）。
2. **专业交易钟状态**：呈现全球主要交易所（沪深、港交所、东证、伦交所、纽交所/纳斯达克）的即时交易状态（开市中、盘前、休市）与时间指示。
3. **高频近实时与节能平衡**：提供可配置的自动轮询机制（15s / 30s / 60s / 暂停），并在后端实施 20 秒高性能内存缓存合并请求；前端配备**页面可见性守卫 (Page Visibility Guard)**，标签页处于后台时自动休眠。
4. **生动的交互视觉 (Matrix Board)**：自适应响应式卡片阵列，集成平滑日内分时走势小图（Sparkline）、行情跳动呼吸闪烁提示（Price Flash），以及可展开的 ECharts 深度分时/K线抽屉。

---

## 2. 后端架构与接口设计 (Backend Architecture & API)

严格遵循 Minefolio DDD 四层 C23 架构与现存 `market` 领域约定。

### 2.1 聚合接口契约
- **HTTP 路径**：`GET /api/market/indices`
- **Controller**：`backend/src/interfaces/http/controllers/market_controller.c` (`api_market_get_indices`)
- **Application Use Case**：`backend/src/application/market/usecases.c` (`market_usecase_get_indices`)
- **Query 参数**（可选）：
  - `category`：过滤指定市场分类（`all` | `cn` | `us` | `hk` | `global` | `crypto`，默认 `all`）。

### 2.2 响应数据结构 (JSON Envelope)
响应采用系统标准 HTTP 200 + `{code, message, data}` 包裹：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "updated_at": "2026-10-16 12:45:00",
    "market_clocks": [
      {
        "market": "SSE_SZSE",
        "name": "A股 (沪深)",
        "timezone": "Asia/Shanghai",
        "status": "open",
        "open_time": "09:30",
        "close_time": "15:00"
      },
      {
        "market": "HKEX",
        "name": "港股",
        "timezone": "Asia/Hong_Kong",
        "status": "open",
        "open_time": "09:30",
        "close_time": "16:00"
      },
      {
        "market": "TSE",
        "name": "东京",
        "timezone": "Asia/Tokyo",
        "status": "closed",
        "open_time": "09:00",
        "close_time": "15:00"
      },
      {
        "market": "LSE",
        "name": "伦敦",
        "timezone": "Europe/London",
        "status": "pre",
        "open_time": "08:00",
        "close_time": "16:30"
      },
      {
        "market": "NYSE",
        "name": "美股",
        "timezone": "America/New_York",
        "status": "closed",
        "open_time": "09:30",
        "close_time": "16:00"
      }
    ],
    "indices": [
      {
        "symbol": "sh000001",
        "name": "上证指数",
        "category": "cn",
        "region_flag": "🇨🇳",
        "current_price": 3285.40,
        "change_amount": 25.32,
        "change_percent": 0.78,
        "open_price": 3262.10,
        "high_price": 3292.10,
        "low_price": 3258.80,
        "prev_close": 3260.08,
        "currency": "CNY",
        "quote_time": "2026-10-16 11:30:00",
        "sparkline": [3260.1, 3265.4, 3270.8, 3268.2, 3280.1, 3285.4]
      }
    ]
  }
}
```

### 2.3 驱动层聚合与内存缓存 (Caching Strategy)
1. **标的分发与多通道并行驱动**：
   - `category == "cn"`：调用 `get_tencent_driver()->fetch_batch`，请求 `sh000001,sz399001,sz399006,sh000300`；
   - `category == "hk"`：调用 `get_tencent_driver()` 或 `get_yahoo_driver()` 请求 `hkHSI,hkHSTECH`；
   - `category == "us"`：调用 `get_yahoo_driver()` 请求 `^GSPC,^IXIC,^DJI`；
   - `category == "global"`：调用 `get_yahoo_driver()` 请求 `^N225,^GDAXI,^FTSE`；
   - `category == "crypto"`：调用 `get_crypto_driver()` 请求 `BTC-USD,ETH-USD`，以及 Yahoo 驱动请求 `GC=F`（黄金）。
2. **高性能内存缓存 (`market_indices_cache_t`)**：
   - 缓存生命周期：TTL = **20 秒**；
   - 使用互斥锁保证多工作线程安全访问；
   - 若当前时间距上次更新小于 20 秒，直接拷贝内存缓存构建 JSON 响应；若已过期，重新拉取驱动并刷新缓存。
3. **离线与测试容灾回退 (Mock Fallback)**：
   - 当外网断开、无网络容器构建或处于单元测试环境时，自动回退到 `driver_mock.c` 生成逼真的指数测试数据，确保测试始终 100% 通过。

### 2.4 全球交易时区与交易钟计算 (Market Clocks Calculation)
在后端内置计算当前市场状态的纯函数：
* 根据服务器 UTC 时间，转换至对应市场的当地时间 `tm_hour`、`tm_min` 与星期（过滤周六周日休市）；
* 判断市场时段：
  - A 股 (Asia/Shanghai)：`09:30-11:30` 与 `13:00-15:00` 判定为 `open`，其余判定为 `closed`；
  - 港股 (Asia/Hong_Kong)：`09:30-12:00` 与 `13:00-16:00` 判定为 `open`；
  - 东京 (Asia/Tokyo)：`09:00-11:30` 与 `12:30-15:00` 判定为 `open`；
  - 伦敦 (Europe/London)：`07:00-08:00` 为 `pre`，`08:00-16:30` 为 `open`；
  - 美股 (America/New_York)：`04:00-09:30` 为 `pre`，`09:30-16:00` 为 `open`，`16:00-20:00` 为 `after`，其余为 `closed`。

---

## 3. 前端架构与交互设计 (Frontend Architecture & UI/UX)

### 3.1 组件组织结构
```text
frontend/src/
├── api/
│   └── market.ts                       # 新增 getIndices(category?: string) 接口定义
├── stores/
│   └── market.ts                       # 扩展 indices、marketClocks、轮询定时器与序列号防竞态
├── components/market/
│   ├── GlobalIndicesBoard.vue          # [总容器] 取代原占位组件，协调子组件与轮询生命周期
│   ├── MarketClocksRibbon.vue          # [子组件] 顶部全球交易钟状态栏与自动刷新控制
│   ├── IndicesFilterBar.vue            # [子组件] 市场分类分段选择与快速过滤
│   ├── IndexGrid.vue                   # [子组件] 响应式网格容器
│   ├── IndexCard.vue                   # [子组件] 单指数卡片（点位、涨跌幅、Sparkline、闪烁动画）
│   └── IndexDetailDrawer.vue           # [子组件] 点击卡片呼出的 ECharts 深度历史走势抽屉
└── types/
    └── index.ts                        # 新增 GlobalIndexItem, MarketClockItem 类型定义
```

### 3.2 核心组件功能交互细节
1. **`MarketClocksRibbon.vue`**：
   - 展现 5 个全球交易所状态药丸胶囊：
     - `交易中`：高亮绿点（带 CSS `animation: pulse` 呼吸灯动画）；
     - `盘前/盘后`：橙黄提示；
     - `已休市`：低对比灰度字样。
   - 右侧控制区：
     - 下拉菜单选择自动刷新间隔：`⚡ 15秒` / `⚡ 30秒(默认)` / `⚡ 60秒` / `关闭`；
     - 刷新按钮：带旋转加载中图标，支持随时手动触发。
2. **`IndicesFilterBar.vue`**：
   - Element Plus 分段单选器：`全部` | `🇨🇳 A股核心` | `🇺🇸 美股三大` | `🇭🇰 港股精选` | `🪙 加密与商品`；
   - 过滤输入框：支持即时模糊过滤当前列表中的名称或代码。
3. **`IndexCard.vue`**：
   - **点位与涨跌**：红涨绿跌（根据中文证券习惯涨红跌绿，或尊重用户红绿偏好），显示如 `+25.32 (+0.78%)`；
   - **数值跳动动画**：比对当前点位与 `marketStore.prevPrices[symbol]`：
     - 价格上升：添加 `.price-flash-up`（背景产生轻量柔和的淡绿高光并在 1 秒内渐隐）；
     - 价格下降：添加 `.price-flash-down`（淡红高光并在 1 秒内渐隐）。
   - **Sparkline 走势微图**：使用轻量级 SVG 平滑折线（基于 Bézier 曲线绘制）并带有渐变填充底色，直观展现日内走势波动；
   - **极值标尺**：清晰呈现 `高: 3,292.10` / `低: 3,258.80`。
4. **`IndexDetailDrawer.vue`**：
   - 抽屉从屏幕右侧滑出（宽度 520px）；
   - 展示大图版 ECharts 折线/K线图，包含鼠标十字丝追踪（Crosshair Tooltip）；
   - 展示详细财务指标（今开、昨收、振幅、成交量、更新时间戳）。

### 3.3 数据流与页面可见性守卫 (Visibility & Timer Lifecycle)
- **定时器生命周期管理**：
  - 在 `GlobalIndicesBoard.vue` 的 `onMounted` 钩子中启动定时轮询；
  - 注册 `document.addEventListener('visibilitychange', onVisibilityChange)`；
  - 当页面最小化或切换到其他浏览器标签页（`document.hidden === true`）时，立即清除 `setInterval` 定时器，停止无效请求；
  - 当页面恢复可见（`document.hidden === false`）时，若距离上次拉取时间已超过间隔，立刻静默拉取一次最新大盘数据并恢复定时器；
  - 在 `onUnmounted` 钩子中彻底销毁定时器并移除事件监听器。
- **并发请求防竞态 (Sequence Guard)**：
  - Store 内部维护 `indicesReqSeq` 自增计数器，确保旧请求因网络延迟后返回时不会覆盖最新请求的结果。
- **数据保留 (Stale-While-Revalidate)**：
  - 若轮询遇到单次瞬时超时，保留现有行情展示，不清除卡片，仅通过控制栏小黄点或微弱提示通知用户网络抖动。

---

## 4. 国际化支持 (i18n)

在 `frontend/src/locales/zh-CN.json` 和 `en-US.json` 中统一扩充大盘指数专有文案：
* `market.indices.title`：「全球大盘指数」 / "Global Market Indices"
* `market.indices.marketClocks`：「全球市场状态」 / "Global Trading Clocks"
* `market.indices.statusOpen`：「交易中」 / "Open"
* `market.indices.statusPre`：「盘前」 / "Pre-Market"
* `market.indices.statusClosed`：「已收盘」 / "Closed"
* `market.indices.refreshInterval`：「自动刷新」 / "Auto Refresh"
* `market.indices.refreshManual`：「立即刷新」 / "Refresh Now"
* `market.indices.categories.all`：「全部」 / "All"
* `market.indices.categories.cn`：「A股核心」 / "China A-Shares"
* `market.indices.categories.us`：「美股三大」 / "US Indices"
* `market.indices.categories.hk`：「港股精选` / "Hong Kong"
* `market.indices.categories.crypto`：「加密与商品」 / "Crypto & Commodities"
* `market.indices.high`：「高」 / "H"
* `market.indices.low`：「低」 / "L"
* `market.indices.prevClose`：「昨收」 / "Prev Close"
* `market.indices.open`：「今开」 / "Open"

---

## 5. 质量保障与自动化测试规划 (Test Plan)

### 5.1 后端 CTest 单元测试 (`backend/tests/unit/test_market_indices.c`)
1. **聚合结果完整性测试**：
   - 验证 `market_usecase_get_indices` 能够正确返回各个分类的关键标的；
   - 验证标的代码规范化与字段（`current_price`, `change_percent`, `sparkline`）有效性。
2. **20 秒内存缓存命中测试**：
   - 第一次调用：缓存未命中，记录更新时间；
   - 立即进行第二次调用：验证返回时间戳完全相同，验证缓存命中逻辑；
   - 模拟时钟前进或过期后调用：验证缓存重新拉取。
3. **离线/Mock 降级测试**：
   - 验证在断网或 mock 模式下仍能稳定返回完备的指标数据结构，保证 CTest 在无网络环境下 100% PASS。

### 5.2 前端 Vitest 单元与组件测试 (`frontend/src/tests/market-indices.test.ts`)
1. **Pinia Store 测试**：
   - 测试 `fetchIndices` 正常拉取赋值、`prevPrices` 点位比对逻辑；
   - 测试并发切换标签时的 `indicesReqSeq` 防竞态有效性；
   - 测试定时器在 `visibilitychange` 事件触发时的暂停与恢复。
2. **组件挂载与渲染测试**：
   - 挂载 `GlobalIndicesBoard.vue` 与 `IndexCard.vue`，验证指数卡片渲染数量与内容；
   - 验证涨跌幅正负对 class（涨绿/跌红）的绑定逻辑；
   - 模拟点击 `IndexCard`，验证 `isDrawerOpen` 状态更新及抽屉展示。

### 5.3 构建与全量回归验证
- 后端构建：`cmake --build backend/build --parallel` 零错误；
- CTest 测试：`cd backend/build && ctest --output-on-failure` 全量通过；
- 前端类型检查与构建：`npm --prefix frontend run build`（`vue-tsc -b && vite build`）零错误；
- 前端测试：`npm --prefix frontend test` 全量通过。
