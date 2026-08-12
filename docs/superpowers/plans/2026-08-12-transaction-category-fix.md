# Transaction Category Feature & Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix transaction creation `category_id` missing bug on `Transactions.vue`, add support for distinct `transaction` categories in backend DB schema, backend APIs, category store, Category Management UI (`Categories.vue`), and auto-seed default transaction categories.

**Architecture:** Extend `categories.type` check constraint to include `'transaction'` in database and C backend, perform an idempotent SQLite table migration for existing databases, automatically seed 5 default transaction categories on first list request, update frontend `CategoryType` union and Pinia store, add Transaction Category support to `Categories.vue`, and fix `el-cascader` event binding and category filtering in `Transactions.vue`.

**Tech Stack:** C (csilk framework, SQLite3), Vue 3, TypeScript, Element Plus, Pinia.

---

### Task 1: Backend Database Migration for Transaction Category

**Files:**
- Modify: `backend/sql/migration.sql:15`
- Modify: `backend/src/common/db.c:50-112`

- [ ] **Step 1: Update SQL migration schema definition**

Update `backend/sql/migration.sql` line 15:
```sql
type       TEXT NOT NULL DEFAULT 'asset' CHECK(type IN ('asset','income','expense','transaction')),
```

- [ ] **Step 2: Add SQLite table migration logic in `db.c`**

In `backend/src/common/db.c`, add an idempotent check in `db_run_migrations` to see if the `categories` table schema contains `'transaction'`. If not, run a schema migration to recreate the `categories` table with the expanded `CHECK` constraint:

```c
    // ---- 交易分类 CHECK 约束迁移 ----
    csilk_json_t* cat_schema = csilk_db_query_json(pool, "SELECT sql FROM sqlite_master WHERE type='table' AND name='categories'");
    if (cat_schema && csilk_json_array_size(cat_schema) > 0) {
        const char* sql_def = csilk_json_get_string(csilk_json_array_get(cat_schema, 0), "sql");
        if (sql_def && !strstr(sql_def, "'transaction'")) {
            csilk_db_exec(pool, "PRAGMA foreign_keys=OFF");
            csilk_db_exec(pool, "BEGIN TRANSACTION");
            csilk_db_exec(pool,
                "CREATE TABLE categories_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
                "  name TEXT NOT NULL,"
                "  parent_id INTEGER REFERENCES categories(id) ON DELETE SET NULL,"
                "  type TEXT NOT NULL DEFAULT 'asset' CHECK(type IN ('asset','income','expense','transaction')),"
                "  asset_type TEXT DEFAULT 'cash' CHECK(asset_type IN ('cash','stock','fund','bond','crypto','real_estate','vehicle','other_asset','loan','credit_card','other_liability')),"
                "  currency TEXT DEFAULT 'CNY',"
                "  icon TEXT,"
                "  sort_order INTEGER DEFAULT 0,"
                "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
                "  UNIQUE(user_id, name, parent_id)"
                ")");
            csilk_db_exec(pool, "INSERT INTO categories_new SELECT * FROM categories");
            csilk_db_exec(pool, "DROP TABLE categories");
            csilk_db_exec(pool, "ALTER TABLE categories_new RENAME TO categories");
            csilk_db_exec(pool, "COMMIT");
            csilk_db_exec(pool, "PRAGMA foreign_keys=ON");
        }
    }
    if (cat_schema) csilk_json_free(cat_schema);
```

- [ ] **Step 3: Build backend and verify migration compilation**

Run:
```bash
make -C backend
```
Expected: Clean build with no errors or warnings.

- [ ] **Step 4: Commit DB migration changes**

```bash
git add backend/sql/migration.sql backend/src/common/db.c
git commit -m "feat(backend): add transaction type to categories check constraint and migration"
```

---

### Task 2: Backend Categories API Default Seeding

**Files:**
- Modify: `backend/src/categories.c:52-115`

- [ ] **Step 1: Implement default seeding in `categories_list`**

In `backend/src/categories.c`, inside `categories_list`:
When `type_query` is `"transaction"`, query if user has any category with `type = 'transaction'`. If total count is 0, seed default transaction categories:
1. `股票/证券` (sort_order: 1)
2. `基金/理财` (sort_order: 2)
3. `外汇/加密` (sort_order: 3)
4. `存现/取现` (sort_order: 4)
5. `交易手续费` (sort_order: 5)

Code snippet in `categories_list`:
```c
    if (type_query && strcmp(type_query, "transaction") == 0) {
        char count_sql[256];
        snprintf(count_sql, sizeof(count_sql),
            "SELECT COUNT(*) as cnt FROM categories WHERE user_id = %lld AND type = 'transaction'",
            (long long)user_id);
        csilk_json_t* cnt_res = csilk_db_query_json(pool, count_sql);
        if (cnt_res && csilk_json_array_size(cnt_res) > 0) {
            int cnt = (int)db_get_num(csilk_json_array_get(cnt_res, 0), "cnt");
            if (cnt == 0) {
                const char* defaults[] = {
                    "股票/证券", "基金/理财", "外汇/加密", "存现/取现", "交易手续费"
                };
                for (int i = 0; i < 5; i++) {
                    char ins_sql[256];
                    snprintf(ins_sql, sizeof(ins_sql),
                        "INSERT INTO categories (user_id, name, type, asset_type, currency, icon, sort_order) "
                        "VALUES (%lld, '%s', 'transaction', 'cash', 'CNY', '', %d)",
                        (long long)user_id, defaults[i], i + 1);
                    csilk_db_exec(pool, ins_sql);
                }
            }
        }
        if (cnt_res) csilk_json_free(cnt_res);
    }
```

- [ ] **Step 2: Build backend and verify API build**

Run:
```bash
make -C backend
```
Expected: Clean build.

- [ ] **Step 3: Commit backend seeding logic**

```bash
git add backend/src/categories.c
git commit -m "feat(backend): auto-seed default transaction categories when requested"
```

---

### Task 3: Frontend Type & Pinia Store Updates

**Files:**
- Modify: `frontend/src/types/index.ts:1`
- Modify: `frontend/src/stores/category.ts:28-38`

- [ ] **Step 1: Update CategoryType union**

In `frontend/src/types/index.ts` line 1:
```typescript
export type CategoryType = 'asset' | 'income' | 'expense' | 'transaction'
```

- [ ] **Step 2: Update Pinia Category Store**

In `frontend/src/stores/category.ts`:
Add `transactionCategories`:
```typescript
  const transactionCategories = computed(() => tree.value.filter(c => c.type === 'transaction'))
```
And export it in store return object:
```typescript
  return { tree, loaded, loading, loadCategories, invalidate, assetCategories, incomeCategories, expenseCategories, incomeExpenseCategories, transactionCategories }
```

- [ ] **Step 3: Verify TypeScript compilation**

Run:
```bash
npm --prefix frontend run build
```
Expected: Frontend builds cleanly without type errors.

- [ ] **Step 4: Commit frontend types & store updates**

```bash
git add frontend/src/types/index.ts frontend/src/stores/category.ts
git commit -m "feat(frontend): add transaction CategoryType and Pinia store computed"
```

---

### Task 4: Frontend Category Management (`Categories.vue`)

**Files:**
- Modify: `frontend/src/views/Categories.vue`

- [ ] **Step 1: Add Tab option and Form Select option**

In `frontend/src/views/Categories.vue`:
1. Add `<el-radio-button value="transaction">交易分类</el-radio-button>` inside `<el-radio-group v-model="activeTab" ...>`:
```html
      <el-radio-group v-model="activeTab" class="category-tabs" @change="onTabChange">
        <el-radio-button value="all">全部分类</el-radio-button>
        <el-radio-button value="asset">资产分类</el-radio-button>
        <el-radio-button value="expense">支出分类</el-radio-button>
        <el-radio-button value="income">收入分类</el-radio-button>
        <el-radio-button value="transaction">交易分类</el-radio-button>
      </el-radio-group>
```

2. Update `activeTab` type definition:
```typescript
const activeTab = ref<'all' | 'asset' | 'expense' | 'income' | 'transaction'>('all')
```

3. Add `<el-option label="交易分类" value="transaction" />` inside form select:
```html
        <el-form-item label="分类大类" prop="type">
          <el-select v-model="form.type" style="width: 100%" @change="onFormTypeChange">
            <el-option label="资产分类" value="asset" />
            <el-option label="支出分类" value="expense" />
            <el-option label="收入分类" value="income" />
            <el-option label="交易分类" value="transaction" />
          </el-select>
        </el-form-item>
```

- [ ] **Step 2: Update Label and Tag helpers**

In `Categories.vue`:
```typescript
function categoryTypeLabel(t: CategoryType) {
  if (t === 'asset') return '资产'
  if (t === 'expense') return '支出'
  if (t === 'income') return '收入'
  if (t === 'transaction') return '交易'
  return t || '资产'
}

function categoryTypeTagType(t: CategoryType) {
  if (t === 'asset') return 'info'
  if (t === 'expense') return 'danger'
  if (t === 'income') return 'success'
  if (t === 'transaction') return 'warning'
  return 'info'
}
```

- [ ] **Step 3: Build frontend and verify**

Run:
```bash
npm --prefix frontend run build
```
Expected: Frontend builds cleanly.

- [ ] **Step 4: Commit Categories.vue updates**

```bash
git add frontend/src/views/Categories.vue
git commit -m "feat(frontend): add transaction category to Category Management view"
```

---

### Task 5: Frontend Transactions Page Cascader & Category Fix (`Transactions.vue`)

**Files:**
- Modify: `frontend/src/views/Transactions.vue`

- [ ] **Step 1: Fix Cascader Event Listener and Syncing**

In `frontend/src/views/Transactions.vue`:
1. Bind `@change="onCatChange"` to `<el-cascader>`:
```html
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :options="categoryTree" :props="{ checkStrictly: true, value: 'id', label: 'name' }" placeholder="选择分类" style="width: 100%" clearable @change="onCatChange" />
        </el-form-item>
```

2. Update `onCatChange` function:
```typescript
function onCatChange(val: any) {
  if (Array.isArray(val) && val.length > 0) {
    form.category_id = Number(val[val.length - 1])
  } else if (val) {
    form.category_id = Number(val)
  } else {
    form.category_id = null
  }
}
```

- [ ] **Step 2: Update Category Fetch to Load `transaction` Type**

In `Transactions.vue`:
In `onMounted`:
```typescript
onMounted(async () => {
  const [assetsRes, catsRes] = await Promise.all([
    assetsApi.list(),
    categoriesApi.list({ type: 'transaction' })
  ])
  assets.value = assetsRes
  allCategories.value = catsRes
  loadData()
})
```

- [ ] **Step 3: Build frontend and test**

Run:
```bash
npm --prefix frontend run build
```
Expected: Clean build.

- [ ] **Step 4: Commit Transactions.vue updates**

```bash
git add frontend/src/views/Transactions.vue
git commit -m "fix(frontend): fix category_id sync in transactions cascader and load transaction categories"
```

---

### Task 6: Full E2E Verification & Integration Testing

- [ ] **Step 1: Build both backend and frontend**

Run:
```bash
make -C backend && npm --prefix frontend run build
```

- [ ] **Step 2: Run automated test scripts or verification checks**

Run backend test script:
```bash
cd backend && ./tests/test_link.sh
```

- [ ] **Step 3: Verify all work**

Confirm that:
1. `make -C backend` compiles with 0 errors.
2. `npm --prefix frontend run build` completes cleanly.
3. Transaction categories work as intended.
