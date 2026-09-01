# Market Quote Sync Enhancements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Yahoo Finance global quote/forex driver, smart trading-hours scheduler, and real-time multi-currency exchange rate conversion across Minefolio.

**Architecture:**
- **Yahoo Finance Driver (`driver_yahoo.c`)**: Connects to `query1.finance.yahoo.com` to fetch global equities, commodities (`GC=F`, `CL=F`), indices (`^GSPC`), and foreign exchange rates (`USDCNY=X`, `HKDCNY=X`, `EURCNY=X`, `JPYCNY=X`, `GBPCNY=X`).
- **Smart Trading-Hours Scheduler (`market_scheduler.c`)**: Intelligently detects China market hours (09:30-11:30, 13:00-15:00), US market hours (21:30-04:00 CST), and 21:30 fund NAV settlement, entering low-frequency power-saving mode during market closure.
- **Multi-Currency Aggregation Engine (`exchange_rate_service.c` & `report_asset_service.c`)**: Provides real-time currency conversion so foreign-denominated assets (USD, HKD, EUR, JPY, GBP) are accurately valued in base currency (CNY) in KPI totals, charts, and table views.

**Tech Stack:**
- Backend: C23, libcurl, csilk JSON, SQLite/PostgreSQL
- Frontend: Vue 3, TypeScript, Pinia, Element Plus, ECharts

---

## Tasks

### Task 1: Yahoo Finance Quote & Forex Driver (`driver_yahoo.c`)
- [ ] Create `backend/src/services/market/driver_yahoo.c` implementing `quote_driver_t` with search and quote fetch for Yahoo symbols (`USDCNY=X`, `HKDCNY=X`, `EURCNY=X`, `JPYCNY=X`, `GC=F`, `^GSPC`, `AAPL`, etc.).
- [ ] Export `get_yahoo_driver(void)` in `backend/src/services/market/quote_driver.h`.
- [ ] Register Yahoo driver in `backend/src/services/market/quote_engine.c`.
- [ ] Update `backend/CMakeLists.txt` to include `driver_yahoo.c`.
- [ ] Verify with test script and commit `feat(market): ✨ add yahoo finance driver for global quotes and forex rates`.

### Task 2: Trading Hours Smart Scheduler (`market_scheduler.c`)
- [ ] Enhance `backend/src/services/market/market_scheduler.c` to detect active trading sessions (CN equities, US equities, evening fund NAV settlement, and 24/7 crypto/forex).
- [ ] Support configurable sync interval & mode in `market_settings` (`trading_hours_only` vs `fixed_interval` vs `manual`).
- [ ] Expose sync mode in `GET /api/market/settings` and `POST /api/market/settings`.
- [ ] Verify scheduler loop behavior and commit `feat(market): ✨ add smart trading hours sync scheduler`.

### Task 3: Multi-Currency Exchange Rate Conversion & Reporting
- [ ] Create `backend/src/services/market/exchange_rate_service.h/.c` maintaining active exchange rate cache (USD/CNY, HKD/CNY, EUR/CNY, JPY/CNY, GBP/CNY) with auto-refresh from Yahoo/Forex driver and sensible fallbacks.
- [ ] Expose `GET /api/market/exchange-rates` for frontend and service consumption.
- [ ] Update `report_asset_service.c`, `ai_tools.c`, `ai_workflow_service.c` to convert foreign currency assets (`current_value * rate`) when calculating total assets, liabilities, net worth, and asset breakdown percentages.
- [ ] Update `frontend/src/api/market.ts` and `frontend/src/views/Assets.vue` / `Holdings.vue` to display native currency alongside converted CNY value (`≈ ¥...`).
- [ ] Run full integration test suite (`test_link.sh` and `test_market_sync.sh`) and frontend type-checking (`npm run build`).
- [ ] Commit `feat(market): ✨ add multi-currency exchange rate conversion and converted net worth aggregation`.
