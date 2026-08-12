# Transactions Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the Transactions page (`frontend/src/views/Transactions.vue`) to eliminate redundant fields (such as `source_type`), dynamically toggle trading inputs (quantity & unit price), introduce auto-calculation of transaction amount, add top summary KPI cards, and enable category filtering for transaction categories.

**Architecture:** Update `frontend/src/views/Transactions.vue` to fetch transaction categories via `categoriesApi.list({ type: 'transaction' })`, compute top KPI stats from fetched transaction data, streamline form controls & validation rules, and optimize table columns for dynamic multi-type display.

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, Vite.

---

### Task 1: Refactor Transactions.vue Page Template & KPI Summary Cards

**Files:**
- Modify: `frontend/src/views/Transactions.vue:1-145`

- [ ] **Step 1: Replace header and add top summary KPI cards**

Update `frontend/src/views/Transactions.vue` template to add `.summary-cards` displaying monthly volume, inflows/investments, outflows/realized, and transaction count. Add category selector to filter bar.

- [ ] **Step 2: Remove redundant `source_type` table column and simplify `quantity`/`price_per_unit` columns**

In the `<el-table>`, remove the `source_type` column and merge `quantity` / `price_per_unit` into a dynamic detail column or subtitle under `amount`.

- [ ] **Step 3: Refactor transaction dialog form controls**

In `<el-dialog>`, remove `source_type` radio group. Conditionally render `quantity` and `price_per_unit` fields only when `form.transaction_type` is `'buy'` or `'sell'`. Add live balance indicator under `asset_id` selector.

- [ ] **Step 4: Verify build**

Run: `npm --prefix frontend run build`
Expected: PASS with 0 build errors.

- [ ] **Step 5: Commit changes**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(frontend): refactor Transactions.vue layout, remove source_type, add top summary cards and conditional form fields"
```

---

### Task 2: Refactor Transactions.vue Logic, Amount Auto-Calculation, and Category Integration

**Files:**
- Modify: `frontend/src/views/Transactions.vue:148-350`

- [ ] **Step 1: Add category filtering and reactive stats logic**

In `<script setup>`, fetch categories with `type: 'transaction'`, compute top KPI values (`monthlyTotalVolume`, `monthlyInflows`, `monthlyOutflows`, `monthlyCount`), and update filter parameters (`category_id`).

- [ ] **Step 2: Implement `quantity` * `price_per_unit` -> `amount` auto-calculation**

Add watchers or event handlers on `form.quantity` and `form.price_per_unit` so that updating either value automatically calculates `form.amount = form.quantity * form.price_per_unit`.

- [ ] **Step 3: Update form validation rules and reset handlers**

Remove `source_type` from `form` reactivity and `rules`. Update `openDialog()` and `resetFilters()` to cleanly clear state.

- [ ] **Step 4: Run full verification build and integration tests**

Run: `npm --prefix frontend run build && cd backend && ./tests/test_link.sh`
Expected: PASS=16 FAIL=0 and clean Vite build.

- [ ] **Step 5: Commit changes**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "feat(frontend): add auto-calculation, category tree scoping, and KPI statistics to Transactions.vue"
```
