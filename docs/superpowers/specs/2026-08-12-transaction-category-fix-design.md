# Design Spec: Add Transaction Category & Fix Transaction Creation

## Summary
Fix the transaction creation bug in the `Transactions.vue` page where `category_id` was not saved from the Cascader input, introduce a distinct `transaction` (交易分类) category type across the application backend and frontend, update Category Management (`Categories.vue`), and automatically seed default transaction categories for users.

---

## 1. Database & Backend Architecture

### 1.1 Database Schema & Migration (`backend/sql/migration.sql` & `backend/src/common/db.c`)
- **Table Constraint**: Update `categories` table schema check constraint to:
  `CHECK(type IN ('asset','income','expense','transaction'))`
- **Migration Logic (`db_run_migrations`)**:
  - SQLite table check constraints cannot be modified with simple `ALTER TABLE`.
  - In `db_run_migrations()`, inspect `sqlite_schema` for `categories` table definition. If `transaction` is not in the check constraint SQL:
    1. Temporarily disable foreign keys (`PRAGMA foreign_keys=OFF`).
    2. Create `categories_new` table with `CHECK(type IN ('asset','income','expense','transaction'))`.
    3. Copy existing records from `categories` into `categories_new`.
    4. Drop `categories` and rename `categories_new` to `categories`.
    5. Re-enable foreign keys (`PRAGMA foreign_keys=ON`).

### 1.2 Category API & Seeding (`backend/src/categories.c`)
- **`categories_list` API**:
  - Automatically handle `type=transaction`.
  - When a user requests categories and has 0 transaction categories in DB, seed a default set of transaction categories for that user:
    1. `股票/证券` (sort_order: 1)
    2. `基金/理财` (sort_order: 2)
    3. `外汇/加密` (sort_order: 3)
    4. `存现/取现` (sort_order: 4)
    5. `交易手续费` (sort_order: 5)
  - After seeding, return the updated category list.

---

## 2. Frontend Design & State Management

### 2.1 Type Definitions (`frontend/src/types/index.ts`)
- Update `CategoryType`:
  `export type CategoryType = 'asset' | 'income' | 'expense' | 'transaction'`

### 2.2 Category Store (`frontend/src/stores/category.ts`)
- Add getter/computed property:
  `const transactionCategories = computed(() => tree.value.filter(c => c.type === 'transaction'))`

### 2.3 Category Management Page (`frontend/src/views/Categories.vue`)
- Add radio button tab for transaction categories:
  `<el-radio-button value="transaction">交易分类</el-radio-button>`
- Add option in Category form select:
  `<el-option label="交易分类" value="transaction" />`
- Update UI helpers:
  - `categoryTypeLabel('transaction')` -> `'交易'`
  - `categoryTypeTagType('transaction')` -> `'warning'`

### 2.4 Transactions Page (`frontend/src/views/Transactions.vue`)
- **Cascader Binding Bug Fix**:
  - Add `@change="onCatChange"` to `<el-cascader>` or bind `onCatChange` so that `form._catPath` selection updates `form.category_id = path[path.length - 1]`.
  - Ensure `openDialog()` initializes `_catPath` and `category_id` correctly.
- **Category Filter Update**:
  - In `onMounted()`, request categories with `type: 'transaction'` (instead of `'income,expense'`).

---

## 3. Testing & Verification
1. Verify database migration upgrades existing SQLite DB without data loss or foreign key errors.
2. Verify category API returns default transaction categories when requested for a new or existing user without transaction categories.
3. Test Category Management page: creating, updating, listing, and deleting `transaction` categories.
4. Test Transaction creation & editing: select a transaction category, save, and ensure `category_id` is properly stored in the backend and displayed in the table.
