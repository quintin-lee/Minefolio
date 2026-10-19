# Chart Interaction Enhancement & Instant-Load SWR Caching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal**: Deliver both Item 4 (Chart DataZoom & PNG/CSV Export) and Item 5 (Pinia LocalStorage Persistence & Stale-While-Revalidate caching for 0ms page instant-load) across desktop and mobile, ensuring zero build or test regressions.

---

## File Structure & Responsibilities

| File | Responsibility |
|---|---|
| `frontend/src/utils/echarts.ts` | Register `DataZoomComponent` and `ToolboxComponent` into modular ECharts |
| `frontend/src/utils/chart-export.ts` | Reusable utilities for exporting chart canvas as high-res PNG and chart series as CSV |
| `frontend/src/utils/swr-cache.ts` | Generic typed Stale-While-Revalidate localStorage caching helper with TTL and error resilience |
| `frontend/src/components/NetWorthChart.vue` | Add inside DataZoom scroll/pinch zoom and PNG/CSV export dropdown |
| `frontend/src/components/PriceHistoryChart.vue` | Add inside DataZoom and export dropdown |
| `frontend/src/components/market/FxTrendChart.vue` | Add inside DataZoom and export dropdown |
| `frontend/src/stores/category.ts` | Initialize from SWR cache; revalidate in background; purge cache on reset |
| `frontend/src/stores/market.ts` | Initialize indices & market clocks from SWR cache; refresh in background |
| `frontend/src/views/Dashboard.vue` | Instant-load summary from SWR cache on mount, eliminating initial skeleton wait |
| `frontend/tests/chart-export.spec.ts` | Unit tests for chart PNG & CSV export functions |
| `frontend/tests/swr-cache.spec.ts` | Unit tests for SWR cache serialization, fallback, expiration, and clearing |

---

## Phase 1: 核心图表交互增强 (DataZoom & Export)

### Task 1.1: ECharts 模块注册与导出工具函数 (TDD)
- [ ] Write unit tests in `frontend/tests/chart-export.spec.ts` verifying:
  - `exportChartPng` triggers download with correct data URL and filename.
  - `exportChartCsv` creates UTF-8 BOM CSV blob and triggers download with correct headers/rows.
- [ ] Run test to verify it fails (`npm --prefix frontend test tests/chart-export.spec.ts`).
- [ ] Register `DataZoomComponent` and `ToolboxComponent` in `frontend/src/utils/echarts.ts`.
- [ ] Implement `frontend/src/utils/chart-export.ts`.
- [ ] Run tests to verify they pass (`npm --prefix frontend test tests/chart-export.spec.ts`).

### Task 1.2: 核心折线图集成 DataZoom 与 导出菜单
- [ ] Update `frontend/src/components/NetWorthChart.vue`:
  - Add `dataZoom: [{ type: 'inside', start: 0, end: 100 }]`.
  - Add export action button (`el-dropdown` with 📸 PNG 导出 & 📊 CSV 导出).
- [ ] Update `frontend/src/components/PriceHistoryChart.vue`:
  - Add `dataZoom: [{ type: 'inside', start: 0, end: 100 }]`.
  - Add export action button.
- [ ] Update `frontend/src/components/market/FxTrendChart.vue`:
  - Add `dataZoom: [{ type: 'inside', start: 0, end: 100 }]`.
  - Add export action button.
- [ ] Run `npm --prefix frontend test` and `npm --prefix frontend run build` to verify no compilation or test errors.
- [ ] Commit: `feat(charts): ✨ add datazoom touch/scroll and png/csv export to trend charts`.

---

## Phase 2: 弱网/秒开体验：Pinia 本地持久化与 SWR 缓存

### Task 2.1: SWR 缓存工具库 (TDD)
- [ ] Write unit tests in `frontend/tests/swr-cache.spec.ts` testing:
  - `getSwrCache`: reads existing cache or falls back to default.
  - `setSwrCache`: serializes payload with timestamp metadata.
  - TTL expiration handling: returns cached data as stale or falls back.
  - `clearSwrCache`: removes keys by prefix or exact match.
  - Handles corrupted JSON without throwing.
- [ ] Run test to verify it fails (`npm --prefix frontend test tests/swr-cache.spec.ts`).
- [ ] Implement `frontend/src/utils/swr-cache.ts`.
- [ ] Run tests to verify they pass (`npm --prefix frontend test tests/swr-cache.spec.ts`).

### Task 2.2: 分类与行情 Store 集成 SWR 缓存
- [ ] Update `frontend/src/stores/category.ts`:
  - Initialize `allNodes` with `getSwrCache('minefolio_categories', [])`.
  - If cached, set `loaded = true` immediately so forms/pickers don't block.
  - Revalidate in background when `loadCategories` is called.
  - Update `reset()` and `invalidate()` to clear or sync the SWR cache.
- [ ] Update `frontend/src/stores/market.ts`:
  - Initialize `indices` and `marketClocks` with SWR cache (`minefolio_cached_indices`, `minefolio_cached_clocks`).
  - Cache latest results in `fetchIndices()`.
- [ ] Run `frontend/tests/category.store.spec.ts` and `frontend/tests/market-store.spec.ts` to ensure full pass.

### Task 2.3: 仪表盘 Dashboard 接入 SWR 秒开
- [ ] Update `frontend/src/views/Dashboard.vue`:
  - Initialize `summary` with `getSwrCache('minefolio_cache_dashboard_summary', defaultSummary)`.
  - If valid cache exists, set `isFirstLoad = false` so dashboard renders immediately in 0ms.
  - On background `summaryApi.get()` completion, write updated summary to SWR cache.
- [ ] Test Dashboard rendering and test suite pass.
- [ ] Commit: `perf(frontend): ⚡️ implement swr caching for instant page loads and pinia stores`.

---

## Phase 3: 全局验证与容器更新
- [ ] Run full test suite: `npm --prefix frontend test`.
- [ ] Run dual-platform builds:
  - `npm --prefix frontend run build` (Desktop)
  - `npm --prefix frontend run build:mobile` (Mobile)
- [ ] Rebuild and restart Docker frontend container:
  `docker build --target nginx -t ghcr.io/quintin-lee/minefolio-frontend:latest . && docker compose up -d nginx`
- [ ] Final git log & status check strictly adhering to `AGENTS.md`.
