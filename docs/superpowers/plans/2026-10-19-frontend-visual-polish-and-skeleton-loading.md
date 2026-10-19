# Frontend Visual Polish & FinTech Aesthetics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Elevate Minefolio's visual experience by replacing disruptive full-screen spinners with tailor-made glassmorphic FinTech skeleton loaders and adding real-time ticker flash animations to market/FX cards.

**Architecture:** 
1. Build reusable, dark-theme aligned skeleton components (`MarketSkeleton.vue`, `FxSkeleton.vue`, `DashboardSkeleton.vue`) with CSS-shimmer gradients matching `--mf-surface-card` and `--mf-border-subtle`.
2. Connect loading states in `GlobalIndicesBoard.vue`, `Market.vue`, and `Dashboard.vue` so skeletons render seamlessly during initial load without layout shifts.
3. Add price delta detection and flash ripple animations (`flash-up` / `flash-down`) to FX rate cards in `FxRateCards.vue` and fix animation termination colors in `IndexCard.vue`.

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, CSS3 keyframe animations, Element Plus design tokens.

**Spec:** Item 1 of Frontend Optimization roadmap: Visual Polish & FinTech Aesthetics.

## Global Constraints

- Adhere to AGENTS.md conventions: Vue 3 + TypeScript strict mode, 0 compile warnings/errors.
- All colors must utilize Minefolio design system tokens (`--mf-surface-card`, `--mf-surface-muted`, `--mf-border-subtle`, `--mf-primary`, etc.).
- Never cause layout shifts (CLS): Skeletons must mirror the exact dimensions, grids, and padding of the actual rendered content.
- Support both dark mode and light mode through CSS variables.

## Review Focus

- Skeleton cards matching actual card layout proportions and margins.
- No flash of unstyled content or double spinners when switching between tabs.
- Price flash animations trigger only when values actually change, fading gracefully to normal card background.
- Zero TypeScript or Vitest test regressions.

---

### Task 1: Add Glassmorphic Skeleton Shimmer Styles to Design System

**Files:**
- Modify: `frontend/src/styles/index.css`

- [ ] **Step 1: Define `.mf-skeleton-shimmer` utility and keyframes in `index.css`**
  Add a smooth linear-gradient animation representing subtle FinTech glass light reflection:
  ```css
  @keyframes mf-shimmer {
    0% { background-position: -200% 0; }
    100% { background-position: 200% 0; }
  }
  .mf-skeleton-block {
    background: linear-gradient(90deg, rgba(255, 255, 255, 0.03) 25%, rgba(255, 255, 255, 0.08) 50%, rgba(255, 255, 255, 0.03) 75%);
    background-size: 200% 100%;
    animation: mf-shimmer 1.8s infinite ease-in-out;
    border-radius: var(--mf-radius-sm);
  }
  [data-theme="light"] .mf-skeleton-block {
    background: linear-gradient(90deg, rgba(0, 0, 0, 0.04) 25%, rgba(0, 0, 0, 0.08) 50%, rgba(0, 0, 0, 0.04) 75%);
    background-size: 200% 100%;
  }
  ```
- [ ] **Step 2: Verify desktop build passes**
  Run `npm --prefix frontend run build` to verify no CSS syntax errors.

---

### Task 2: Create Market Skeleton Component & Integrate into Global Indices Board

**Files:**
- Create: `frontend/src/components/market/MarketSkeleton.vue`
- Modify: `frontend/src/components/market/GlobalIndicesBoard.vue`

- [ ] **Step 1: Implement `MarketSkeleton.vue`**
  Build skeletons for:
  - Ribbon: label placeholder + 5 pill placeholders + interval selector
  - Filter bar: 6 category pill placeholders + search box placeholder
  - Grid: 8 index card skeletons with flag, name, price, sparkline wave, and high/low stats.
- [ ] **Step 2: Integrate `MarketSkeleton.vue` into `GlobalIndicesBoard.vue`**
  Render `<MarketSkeleton v-if="marketStore.isLoadingIndices && marketStore.indices.length === 0" />` instead of empty board with center spinner.
- [ ] **Step 3: Fix `IndexCard.vue` keyframe background termination**
  Update `@keyframes flash-green` and `@keyframes flash-red` in `IndexCard.vue` to transition to `var(--mf-surface-card)` instead of `var(--el-bg-color-overlay)`.
- [ ] **Step 4: Verify build and component rendering**
  Run `npm --prefix frontend test` and `npm --prefix frontend run build`.

---

### Task 3: Create FX Skeleton Component & Real-time Ticker Flash for FX Cards

**Files:**
- Create: `frontend/src/components/market/FxSkeleton.vue`
- Modify: `frontend/src/views/Market.vue`
- Modify: `frontend/src/components/market/FxRateCards.vue`
- Modify: `frontend/src/stores/market.ts`

- [ ] **Step 1: Track FX price directions in `market.ts`**
  Add `fxDirections = ref<Record<string, 'up' | 'down' | 'same'>>({})` and compare previous rates during `fetchRates()` to detect live changes.
- [ ] **Step 2: Add flash animation to `FxRateCards.vue`**
  Apply `:class="[fxDirections[code] === 'up' ? 'flash-up' : fxDirections[code] === 'down' ? 'flash-down' : '']"` to `.rate-card` with matching green/red ripple keyframes.
- [ ] **Step 3: Build `FxSkeleton.vue`**
  Create 8 rate card placeholders and 2 large split card placeholders (trend chart and converter).
- [ ] **Step 4: Integrate `FxSkeleton.vue` into `Market.vue`**
  Show `<FxSkeleton />` when FX data is loading on initial mount.
- [ ] **Step 5: Verify build**
  Run `npm --prefix frontend run build`.

---

### Task 4: Create Dashboard Bento Skeleton & Polish Dashboard Initial Load

**Files:**
- Create: `frontend/src/components/dashboard/DashboardSkeleton.vue`
- Modify: `frontend/src/views/Dashboard.vue`

- [ ] **Step 1: Build `DashboardSkeleton.vue`**
  Mirror the Bento Grid layout:
  - Hero Net Worth card skeleton (badge, large amount, sparkline area)
  - 3 Secondary asset/liability/PnL card skeletons
  - 2 Chart card skeletons (Net Worth trend & Asset allocation pie)
- [ ] **Step 2: Integrate skeleton into `Dashboard.vue`**
  Render `DashboardSkeleton` while `loading && !summary.net_worth`, eliminating initial blank frame or harsh full-page mask.
- [ ] **Step 3: Run full verification**
  Run Vitest test suite (`npm test`), desktop build (`npm run build`), and mobile build (`npm run build:mobile`).
- [ ] **Step 4: Commit**
  Commit changes following Conventional Commits: `feat(ui): ✨ add glassmorphic FinTech skeletons and real-time ticker flash animations`.
