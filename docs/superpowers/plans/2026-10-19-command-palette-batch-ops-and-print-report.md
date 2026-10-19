# Command Palette, Batch Table Operations & Print Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal**: Implement Item 1 (Global Command Palette `Ctrl+K`), Item 4 (Batch Operations & Floating Action Bar for Daily Expenses and Transactions), and Item 5 (Print-Optimized Financial Report & PDF Export for Reports.vue).

---

## File Structure & Responsibilities

| File | Responsibility |
|---|---|
| `frontend/src/components/CommandPalette.vue` | Command Palette dialog with route search, quick actions (quick add, theme toggle, market sync), asset direct search, and keyboard navigation (`Ctrl/Cmd+K`, Arrow keys, Enter) |
| `frontend/src/views/Layout.vue` | Mount Command Palette, add global keyboard shortcut listener and header quick-search input button |
| `frontend/src/views/DailyExpenses.vue` | Add selection column, selection state, and sliding Floating Action Bar for batch deletion and batch CSV export |
| `frontend/src/views/Transactions.vue` | Add selection column, selection state, and sliding Floating Action Bar for batch deletion and batch CSV export |
| `frontend/src/views/Reports.vue` | Add "打印 / 导出 PDF" header action and comprehensive `@media print` layout styles |
| `frontend/tests/command-palette.spec.ts` | Unit tests for command palette filtering, search scoring, and action emission |

---

## Phase 1: 全局指令搜索面板 Command Palette (`Ctrl/Cmd + K`)

### Task 1.1: 编写指令面板单元测试 (TDD)
- [ ] Create `frontend/tests/command-palette.spec.ts` testing:
  - Filtering static navigation routes (e.g. typing "资产" or "asset" filters to `/assets`).
  - Filtering quick actions (e.g. "记账", "主题", "行情").
  - Arrow key navigation & Enter key execution.
- [ ] Run test to verify failure (`npm --prefix frontend test tests/command-palette.spec.ts`).
- [ ] Implement `frontend/src/components/CommandPalette.vue`.
- [ ] Run test to verify pass (`npm --prefix frontend test tests/command-palette.spec.ts`).

### Task 1.2: 布局层挂载与全局快捷键绑定
- [ ] In `frontend/src/views/Layout.vue`:
  - Mount `CommandPalette` component.
  - Bind global `keydown` listener for `(e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k'`.
  - Add search button trigger in `.header-left` (e.g. `🔍 搜索或按 Ctrl+K`).
- [ ] Run `npm --prefix frontend test` to verify no regressions.
- [ ] Commit: `feat(command-palette): ✨ add global ctrl+k command palette with fuzzy navigation and actions`.

---

## Phase 2: 交易流水批量操作 (Batch Selection & Floating Bar)

### Task 2.1: `DailyExpenses.vue` 表格多选与悬浮动作条
- [ ] In `frontend/src/views/DailyExpenses.vue`:
  - Add `<el-table-column type="selection" width="48" />`.
  - Track `selectedExpenses` on `@selection-change`.
  - Add sliding bottom Floating Action Bar (`.floating-batch-bar`) with:
    - Display "已选择 N 项记录".
    - 【批量删除】with confirmation modal and parallel delete calls.
    - 【导出所选】calling `exportChartCsv`.
    - 【取消选择】calling `tableRef.clearSelection()`.
  - Add glassmorphic floating bar CSS with smooth slide-up animation.

### Task 2.2: `Transactions.vue` 表格多选与悬浮动作条
- [ ] In `frontend/src/views/Transactions.vue`:
  - Add `<el-table-column type="selection" width="48" />`.
  - Track `selectedTransactions` on `@selection-change`.
  - Add sliding bottom Floating Action Bar with batch delete, batch export, and clear selection.
  - Ensure parent/child fee rows and transfers are safely handled during deletion.
- [ ] Verify test suite: `npm --prefix frontend test`.
- [ ] Commit: `feat(table): ✨ add batch selection and floating action bar to expenses and transactions`.

---

## Phase 3: 专业财务报表打印与 PDF 导出 (`Reports.vue`)

### Task 3.1: 报表打印操作与 `@media print` 专业排版
- [ ] In `frontend/src/views/Reports.vue`:
  - Add header action button: `<el-button class="action-btn" :icon="Printer" @click="handlePrint">打印 / 导出 PDF</el-button>`.
  - Add print-only header block: "Minefolio 财务分析报告 - [月份/年份]".
  - Add `@media print` CSS:
    - Hide aside, header, tabs header, date pickers, scrollbars, and buttons.
    - Expand `.reports-page` and report cards to 100% width.
    - Set `-webkit-print-color-adjust: exact` and `page-break-inside: avoid` for `.report-card`.
    - Ensure crisp black/gray text and clean chart rendering on paper/PDF.
- [ ] Verify build and tests: `npm --prefix frontend test` and `npm --prefix frontend run build`.
- [ ] Commit: `feat(reports): ✨ add print and pdf export with dedicated print stylesheet`.

---

## Phase 4: 全局双端验证与生产容器部署
- [ ] Full tests: `npm --prefix frontend test`.
- [ ] Desktop build: `npm --prefix frontend run build`.
- [ ] Mobile build: `npm --prefix frontend run build:mobile`.
- [ ] Rebuild and restart Docker frontend:
  `docker build --target nginx -t ghcr.io/quintin-lee/minefolio-frontend:latest . && docker compose up -d nginx`
- [ ] Verify `AGENTS.md` commit convention and cleanliness.
