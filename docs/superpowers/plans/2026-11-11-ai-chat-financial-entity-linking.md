# AI Chat Financial Entity Linking & Micro-Cards Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform static financial text in AI chat messages into interactive, live entity badges with rich hover micro-cards and seamless deep-linking to asset/account details in `Assets.vue`.

**Architecture:** Client-side dictionary-based entity recognition (`useFinancialEntities.ts`) indexing user accounts, assets, and holdings reports. Safely enhances completed HTML text nodes with `.mf-entity-badge` tags. A centralized `FinancialEntityPopover.vue` provides real-time valuation, cost-basis, and PnL metrics with deep-link navigation into `Assets.vue`, which auto-expands groups and scrolls target cards into view.

**Tech Stack:** Vue 3 (Composition API, `<script setup>`), TypeScript, Vitest, Vue Router, Element Plus, Iconify.

**Spec:** `docs/superpowers/specs/2026-11-11-ai-chat-financial-entity-linking-design.md`

## Global Constraints

- Pure client-side parsing and enhancement; zero changes to backend C23 APIs or LLM prompts.
- Stopwords blacklist: "默认账户", "现金账户", "总资产", "总负债", "净资产", and any single-character word (length < 2).
- Non-destructive HTML text-node replacement: never match or alter text inside `<a>`, `<code>`, `<pre>`, or Mermaid diagrams.
- Streaming safety: raw text streams smoothly without entity replacement overhead; entity badges apply only to completed segments.
- Financial typography: monospace font for currency and percentages; profit/loss red/green colors strictly isolated to verified holding PnL in micro-cards.

## Review Focus

- Longest-word match precedence: "招商银行信用卡" matches before "招商银行".
- HTML tag boundary isolation: text like `<a href="/test">招商银行</a>` or `<code>const asset = "腾讯控股"</code>` must not be injected with inner badges.
- Disambiguation handling: when an Account and an Asset share the identical name, `FinancialEntityPopover` provides an intuitive tab switcher `[资产 | 账户]`.
- Route deep-linking: navigating to `/assets?asset_id=12` correctly expands the parent account group, scrolls into view, and flashes the highlight class.
- Mobile tap interaction: on mobile touch devices, clicking an entity badge opens the micro-card, and tapping outside closes it.

---

### Task 1: `useFinancialEntities.ts` Composable & Unit Tests

**Files:**
- Create: `frontend/src/composables/useFinancialEntities.ts`
- Create: `frontend/src/composables/__tests__/useFinancialEntities.spec.ts`

**Interfaces:**
- Produces:
  ```typescript
  export interface EntityDetail {
    id: string | number
    name: string
    type: 'asset' | 'account'
    icon?: string
    institution?: string
    accountName?: string
    assetType?: string
    currentValue?: number
    costBasis?: number
    quantity?: number
    floatingPnl?: number
    floatingPct?: number
    currency?: string
    assetCount?: number
  }

  export function useFinancialEntities(): {
    loadEntities: () => Promise<void>
    linkFinancialEntities: (html: string) => string
    getEntityDetail: (type: 'asset' | 'account', id: string | number) => EntityDetail | null
    getCollisionEntities: (name: string) => { asset?: EntityDetail; account?: EntityDetail } | null
  }
  ```

- [ ] **Step 1: Write failing unit test in `frontend/src/composables/__tests__/useFinancialEntities.spec.ts`**

```typescript
import { describe, it, expect } from 'vitest'
import { buildEntityRegex, replaceTextNodesWithEntities, isStopword } from '../useFinancialEntities'

describe('useFinancialEntities', () => {
  it('filters out system stopwords and single characters', () => {
    expect(isStopword('默认账户')).toBe(true)
    expect(isStopword('现金账户')).toBe(true)
    expect(isStopword('卡')).toBe(true)
    expect(isStopword('腾讯控股')).toBe(false)
    expect(isStopword('招商银行')).toBe(false)
  })

  it('sorts patterns by length descending for longest-first matching', () => {
    const names = ['招商银行', '招商银行储蓄卡', '银行']
    const regex = buildEntityRegex(names)
    const match = '我的招商银行储蓄卡已到期'.match(regex)
    expect(match?.[0]).toBe('招商银行储蓄卡')
  })

  it('replaces only plain text and ignores tags/code blocks', () => {
    const html = '<p>请查看腾讯控股行情，参考 <code>腾讯控股代码</code> 和 <a href="#">腾讯控股官网</a></p>'
    const lookup = new Map([['腾讯控股', { type: 'asset', id: 101, name: '腾讯控股' }]])
    const result = replaceTextNodesWithEntities(html, lookup)
    expect(result).toContain('<span class="mf-entity-badge" data-entity-type="asset" data-entity-id="101">腾讯控股</span>')
    expect(result).toContain('<code>腾讯控股代码</code>')
    expect(result).toContain('<a href="#">腾讯控股官网</a>')
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm --prefix frontend test frontend/src/composables/__tests__/useFinancialEntities.spec.ts`
Expected: FAIL with module not found or export missing.

- [ ] **Step 3: Implement `useFinancialEntities.ts`**

Implement:
1. `isStopword(word: string): boolean` checking predefined blacklist and length < 2.
2. `buildEntityRegex(names: string[]): RegExp` sorting by length descending and escaping regex tokens.
3. `replaceTextNodesWithEntities(html: string, lookup: Map<string, EntityMatch>): string` utilizing browser DOMParser (or regex text tokenization compatible with SSR/Vitest jsdom).
4. `loadEntities()` fetching `accountsApi.list()`, `assetsApi.list()`, and `reportsApi.holdings()`, aggregating into in-memory cached lookup maps.
5. Export composable `useFinancialEntities()`.

- [ ] **Step 4: Run test to verify it passes**

Run: `npm --prefix frontend test frontend/src/composables/__tests__/useFinancialEntities.spec.ts`
Expected: PASS with 3 passing tests.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/composables/useFinancialEntities.ts frontend/src/composables/__tests__/useFinancialEntities.spec.ts
git commit -m "feat(chat): 🧠 implement financial entity linking composable with tests"
```

---

### Task 2: `FinancialEntityPopover.vue` Component

**Files:**
- Create: `frontend/src/components/FinancialEntityPopover.vue`

**Interfaces:**
- Consumes:
  `EntityDetail` from `useFinancialEntities.ts`
- Produces:
  ```typescript
  defineProps<{
    visible: boolean
    targetRect: DOMRect | null
    detail: EntityDetail | null
    collision?: { asset?: EntityDetail; account?: EntityDetail } | null
  }>()

  defineEmits<{
    (e: 'close'): void
    (e: 'navigate', payload: { type: 'asset' | 'account'; id: string | number }): void
  }>()
  ```

- [ ] **Step 1: Create `FinancialEntityPopover.vue`**

Features:
1. Fixed-position popover computed from `targetRect` with boundary overflow protection (stays within viewport).
2. Segmented switcher `[资产] | [账户]` rendered when `collision` contains both an asset and an account.
3. Visuals:
   - Header: Entity icon (`getAssetIcon` / `getAccountIcon`), title, subtitle (e.g. `股票 · 所属招商银行卡` or `平台账户 · 4 项资产`).
   - Body stats:
     - 实时估值/余额: `formatCurrency(detail.currentValue)`
     - 投资品 PnL: `formatSigned(detail.floatingPnl)` and `+X.XX%` with `--mf-gain` / `--mf-loss` colors.
     - 持仓数量与成本基准 (if asset).
   - Footer action: button "前往资产详情 →" or "查看账户下资产 →" emitting `navigate`.

- [ ] **Step 2: Typecheck and build verification**

Run: `npm --prefix frontend run build`
Expected: PASS with 0 TypeScript errors.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/FinancialEntityPopover.vue
git commit -m "feat(chat): 🎴 create financial entity hover micro-card component"
```

---

### Task 3: Deep-linking & Highlight in `Assets.vue`

**Files:**
- Modify: `frontend/src/views/Assets.vue`

**Interfaces:**
- Consumes:
  `route.query.asset_id` and `route.query.account_id`
- Produces:
  Auto-expansion of corresponding `account-group`, smooth scroll into view, and temporary `highlight-pulse` animation on target asset card or account group.

- [ ] **Step 1: Add route query inspector and auto-scroll in `Assets.vue`**

1. Inject `useRoute` and watch `[() => route.query.asset_id, () => route.query.account_id]`.
2. Function `handleDeepLinkTarget()`:
   - If `account_id`: find target group in `groupedAccounts`, ensure `collapsedGroups.delete(groupKey)`, wait `nextTick`, scroll `account-group-header` into view, apply temporary CSS class `highlight-pulse`.
   - If `asset_id`: locate parent group containing `a.id === Number(asset_id)`, ensure parent group is uncollapsed, wait `nextTick`, scroll `asset-row` into view, apply temporary CSS class `highlight-pulse`.
3. Add CSS `.highlight-pulse`:
   ```css
   @keyframes entityPulse {
     0% { box-shadow: 0 0 0 0 rgba(var(--mf-primary-rgb), 0.5); }
     50% { box-shadow: 0 0 0 6px rgba(var(--mf-primary-rgb), 0.25); }
     100% { box-shadow: 0 0 0 0 transparent; }
   }
   .highlight-pulse {
     animation: entityPulse 2s ease-out;
     border-color: var(--mf-primary) !important;
   }
   ```

- [ ] **Step 2: Run frontend build to verify compilation**

Run: `npm --prefix frontend run build`
Expected: PASS with 0 errors.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/views/Assets.vue
git commit -m "feat(assets): 🔗 support deep-link query navigation with highlight pulse"
```

---

### Task 4: Integrate Entity Linking & Micro-Cards into `ChatMessageContent.vue` and `Chat.vue`

**Files:**
- Modify: `frontend/src/components/ChatMessageContent.vue`
- Modify: `frontend/src/views/Chat.vue`

**Interfaces:**
- Consumes:
  `useFinancialEntities` from Task 1, `FinancialEntityPopover.vue` from Task 2.

- [ ] **Step 1: Integrate entity linking in `ChatMessageContent.vue`**

1. In `ChatMessageContent.vue`, call `linkFinancialEntities` inside `renderMarkdown` after `marked.parse` & `DOMPurify.sanitize` (only on completed Markdown segments, not on `streaming-text`).
2. Add badge styling `.mf-entity-badge`:
   ```css
   :deep(.mf-entity-badge) {
     display: inline-flex;
     align-items: center;
     padding: 0 5px;
     margin: 0 2px;
     border-radius: 4px;
     background: var(--mf-primary-light);
     color: var(--mf-primary);
     font-weight: 500;
     cursor: pointer;
     transition: all 0.15s ease;
     border: 1px solid var(--mf-primary-border);
   }
   :deep(.mf-entity-badge:hover) {
     background: var(--mf-primary);
     color: #ffffff;
   }
   :deep(.mf-amount-pill) {
     font-family: var(--mf-font-mono, monospace);
     font-weight: 600;
     letter-spacing: -0.2px;
   }
   ```
3. Emit event `entity-hover` and `entity-click` with DOM element, type, and id.

- [ ] **Step 2: Mount `FinancialEntityPopover` in `Chat.vue`**

1. Add `FinancialEntityPopover` instance in `Chat.vue`.
2. Manage popover trigger with 180ms hover delay / 220ms leave delay.
3. On `navigate` event from popover:
   - Call `router.push({ path: '/assets', query: payload.type === 'asset' ? { asset_id: payload.id } : { account_id: payload.id } })`.

- [ ] **Step 3: Verify TypeScript and Vite builds**

Run: `npm --prefix frontend run build`
Expected: PASS with 0 errors.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/ChatMessageContent.vue frontend/src/views/Chat.vue
git commit -m "feat(chat): 🎯 connect entity badges to interactive hover micro-cards"
```

---

### Task 5: Mobile Compatibility & Full Verification

**Files:**
- Modify/Verify: `frontend/src/components/FinancialEntityPopover.vue`
- Modify/Verify: `frontend/src/views/Chat.vue`

- [ ] **Step 1: Verify and handle mobile tap interactions**

Ensure touch events (`touchstart`/`click`) on `.mf-entity-badge` properly toggle the popover on touch screens, and clicking outside or scrolling dismisses it.

- [ ] **Step 2: Run all test suites and builds**

1. Run: `npm --prefix frontend test`
2. Run: `npm --prefix frontend run build`
3. Run: `npm --prefix frontend run build:mobile`

- [ ] **Step 3: Commit and clean working tree**

```bash
git status
```
Ensure working tree is clean.
