# ECharts Modular Bundle Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce desktop frontend bundle size by ~500KB by migrating `FxTrendChart.vue` and `IndexDetailDrawer.vue` to the lightweight on-demand `@/utils/echarts` module and eliminating redundant full-library `echarts` imports.

**Architecture:**
1. Align all ECharts components to import from the centralized modular wrapper `@/utils/echarts.ts`.
2. Ensure `@/utils/echarts.ts` registers all required modules (`LineChart`, `BarChart`, `PieChart`, `GridComponent`, `TooltipComponent`, `LegendComponent`, `TitleComponent`, `CanvasRenderer`).
3. Verify that zero direct runtime imports from `'echarts'` remain across `src/`.
4. Run full TypeScript checks, Vitest test suites, and production builds (`npm run build` and `npm run build:mobile`) to verify chunk size reduction from 1,042 kB down to ~535 kB.

**Tech Stack:** Vue 3, TypeScript, ECharts 5 (core + charts + components + renderers), Vite / Rollup.

**Spec:** Item 4 of Frontend Optimization: ECharts 模块按需引入与构建瘦身.

## Global Constraints

- Strict adherence to AGENTS.md conventions.
- No breakage of any chart rendering (Asset Trend, Net Worth, Monthly, Yearly, Holdings Pie, FX Trend, Index Mini Trend).
- All Vitest unit tests (105 tests) must continue passing.
- Desktop and mobile builds must exit 0.

## Review Focus

- Correct types (`ECharts`, `EChartsOption`) imported from `@/utils/echarts`.
- `echarts.graphic.LinearGradient` works identically in FX and Index charts without runtime errors.
- Confirm `vendor-echarts` size in `npm run build` is reduced to ~535 kB (matching mobile).

---

### Task 1: Refactor `FxTrendChart.vue` and `IndexDetailDrawer.vue` to Use Modular ECharts

**Files:**
- Modify: `frontend/src/components/market/FxTrendChart.vue:70-110`
- Modify: `frontend/src/components/market/IndexDetailDrawer.vue:55-95`

- [ ] **Step 1: Update `FxTrendChart.vue` imports and types**
  Replace `import * as echarts from 'echarts'` with `import echarts, { type ECharts, type EChartsOption } from '@/utils/echarts'`.
  Update `let chartInstance: ECharts | null = null` and `const option: EChartsOption = { ... }`.
- [ ] **Step 2: Update `IndexDetailDrawer.vue` imports and types**
  Replace `import * as echarts from 'echarts'` with `import echarts, { type ECharts, type EChartsOption } from '@/utils/echarts'`.
  Update `let chartInstance: ECharts | null = null` and `const option: EChartsOption = { ... }`.
- [ ] **Step 3: Verify no direct runtime `from 'echarts'` imports remain**
  Verify with `grep -rn "from 'echarts'" frontend/src/` that only `import type { EChartsOption } from 'echarts'` in `utils/echarts.ts` remains.

---

### Task 2: Build Verification & Bundle Measurement

**Files:**
- Verify: `npm --prefix frontend test`
- Verify: `npm --prefix frontend run build`
- Verify: `npm --prefix frontend run build:mobile`

- [ ] **Step 1: Run Vitest tests**
  Execute `npm --prefix frontend test` to ensure zero regressions across all 20 test files.
- [ ] **Step 2: Run production desktop build**
  Execute `npm --prefix frontend run build` and measure `vendor-echarts` size reduction.
- [ ] **Step 3: Run production mobile build**
  Execute `npm --prefix frontend run build:mobile`.
- [ ] **Step 4: Rebuild Nginx container and restart**
  Run `docker build --target nginx -t ghcr.io/quintin-lee/minefolio-frontend:latest . && docker compose up -d nginx`.

---

### Task 3: Commit

- [ ] **Step 1: Commit with Conventional Commits**
  Commit message: `perf(frontend): ⚡️ switch market charts to modular echarts and reduce bundle by 500kb`.
