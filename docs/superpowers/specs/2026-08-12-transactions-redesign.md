# Transactions Page Redesign Specification

## 1. Overview
The Transactions page (`frontend/src/views/Transactions.vue`) is being refactored to streamline transaction tracking, remove redundant UI elements, and introduce essential interactive features.

## 2. Redundancies Removed
- **`source_type` (收支) Field & Column**: Removed from both form and table view. Each `transaction_type` (buy, sell, deposit, withdrawal, etc.) inherently dictates direction.
- **Static Quantity/Price Inputs**: Form inputs for `quantity` and `price_per_unit` are conditionally rendered only for trading types (`buy`, `sell`). For non-trading types, they are hidden to reduce cognitive load.
- **Sparse Table Columns**: Replaced separate `quantity` and `price_per_unit` columns with a combined dynamic detail view (showing `单价 × 数量` under the amount or as a sub-label when applicable).

## 3. New Features & Improvements
- **Top Summary KPI Cards**:
  - 本月交易总额 (Total Monthly Volume)
  - 存入/买入合计 (Total Inflows/Investments)
  - 取出/卖出合计 (Total Outflows/Realized)
  - 本月交易笔数 (Monthly Count)
- **Amount Auto-Calculation**:
  - Entering `quantity` and `price_per_unit` automatically computes `amount = quantity * price_per_unit`.
- **Asset Balance Preview**:
  - Selecting an asset account in the dialog displays its current available balance.
- **Category Filter & Transaction Category Scope**:
  - Filter bar includes category selection (`category_id`).
  - Category selector strictly uses `transaction` category types.

## 4. Architectural & API Impact
- Frontend: Updates `Transactions.vue` to use `categoriesApi` with `type=transaction`, computes KPIs locally from fetched dataset, and streamlines form rules.
- Backend: Compatible with existing `/api/transactions` parameter-bound endpoints.
