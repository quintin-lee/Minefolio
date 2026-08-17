# Project Health Fixes Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix P0 security/data issues and add missing mobile holdings page identified in project analysis.

**Architecture:** Three independent tasks: (1) fix unsafe JSON parsing in C backend, (2) enable CSRF protection by default in Docker, (3) implement mobile holdings view. Each task is self-contained with clear file boundaries.

**Tech Stack:** C23, csilk framework, Vue 3, TypeScript, Element Plus

---

## Chunk 1: Fix JSON Parsing Inconsistency

**Spec reference:** `docs/superpowers/specs/2026-08-17-project-analysis-design.md` - Section "问题 1：JSON 数值解析不一致"

### Task 1.1: Fix categories.c JSON parsing

**Files:**
- Modify: `backend/src/categories.c:372-373`
- Modify: `backend/src/categories.c:423`

- [ ] **Step 1: Read current code**

```bash
sed -n '370,375p' backend/src/categories.c
sed -n '420,425p' backend/src/categories.c
```

- [ ] **Step 2: Fix categories_create - replace csilk_json_get_number with db_get_num**

```c
// Before (categories.c:372-373):
int64_t parent_id = (int64_t)csilk_json_get_number(body, "parent_id");
int sort_order = (int)csilk_json_get_number(body, "sort_order");

// After:
const csilk_json_t* parent_id_val = csilk_json_get(body, "parent_id");
int64_t parent_id = parent_id_val ? (int64_t)db_get_num(parent_id_val, "") : 0;
const csilk_json_t* sort_order_val = csilk_json_get(body, "sort_order");
int sort_order = sort_order_val ? (int)db_get_num(sort_order_val, "") : 0;
```

Wait - the above is wrong. `db_get_num()` takes the JSON object and key. Let me re-read the API:

```c
// Correct approach:
int64_t parent_id = (int64_t)db_get_num(body, "parent_id");
int sort_order = (int)db_get_num(body, "sort_order");
```

- [ ] **Step 3: Apply fix to categories.c**

Use `edit` tool to replace:
```
    int64_t parent_id = (int64_t)csilk_json_get_number(body, "parent_id");
    int sort_order = (int)csilk_json_get_number(body, "sort_order");
```
with:
```
    int64_t parent_id = (int64_t)db_get_num(body, "parent_id");
    int sort_order = (int)db_get_num(body, "sort_order");
```

And at line 423, replace:
```
    int sort_order = (int)csilk_json_get_number(body, "sort_order");
```
with:
```
    int sort_order = (int)db_get_num(body, "sort_order");
```

- [ ] **Step 4: Build backend to verify**

```bash
cd backend && cmake --build build --parallel
```

Expected: Build succeeds with no errors

- [ ] **Step 5: Run integration tests**

```bash
cd backend && bash tests/test_link.sh
```

Expected: `PASS=103 FAIL=0`

- [ ] **Step 6: Commit**

```bash
git add backend/src/categories.c
git commit -m "fix(db): use db_get_num for safe JSON number parsing in categories"
```

### Task 1.2: Fix daily_expenses.c JSON parsing

**Files:**
- Modify: `backend/src/daily_expenses.c:328-331`

- [ ] **Step 1: Apply fixes**

Replace lines 328-331:
```c
    int64_t category_id = (int64_t)csilk_json_get_number(body, "category_id");
    int64_t asset_id = (int64_t)csilk_json_get_number(body, "asset_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = csilk_json_get_number(body, "amount");
```

With:
```c
    int64_t category_id = (int64_t)db_get_num(body, "category_id");
    int64_t asset_id = (int64_t)db_get_num(body, "asset_id");
    const char* type = csilk_json_get_string(body, "expense_type");
    double amount = db_get_num(body, "amount");
```

- [ ] **Step 2: Build and test**

```bash
cd backend && cmake --build build --parallel && bash tests/test_link.sh
```

Expected: Build succeeds, `PASS=103 FAIL=0`

- [ ] **Step 3: Commit**

```bash
git add backend/src/daily_expenses.c
git commit -m "fix(db): use db_get_num for safe JSON number parsing in daily_expenses"
```

---

## Chunk 2: Enable CSRF by Default

**Spec reference:** `docs/superpowers/specs/2026-08-17-project-analysis-design.md` - Section "SEC-001: CSRF 默认关闭"

### Task 2.1: Update docker-compose.yml

**Files:**
- Modify: `docker-compose.yml`

- [ ] **Step 1: Add MINEFOLIO_ENABLE_CSRF to docker-compose**

```yaml
services:
  minefolio:
    build:
      context: .
      dockerfile: Dockerfile
      target: runtime
    image: minefolio:latest
    container_name: minefolio
    expose:
      - "8080"
    environment:
      MINEFOLIO_JWT_SECRET: "${MINEFOLIO_JWT_SECRET:-change-me-in-production}"
      MINEFOLIO_DB_DSN: "/app/data/minefolio.db"
      MINEFOLIO_ENABLE_CSRF: "1"
    volumes:
      - minefolio-data:/app/data
    restart: unless-stopped
```

- [ ] **Step 2: Create .env file if not exists**

```bash
if [ ! -f .env ]; then
  cp .env.example .env 2>/dev/null || echo "MINEFOLIO_JWT_SECRET=change-me-in-production" > .env
fi
```

- [ ] **Step 3: Verify docker compose works**

```bash
docker compose up -d --build
sleep 5
curl -s http://localhost/api/system/status | jq .
```

Expected: Service starts, status returns JSON

- [ ] **Step 4: Test CSRF protection**

```bash
# Should fail without CSRF token
curl -s -X POST http://localhost/api/assets -H "Content-Type: application/json" -d '{}'
# Expected: 403 Forbidden
```

- [ ] **Step 5: Commit**

```bash
git add docker-compose.yml
git commit -m "chore: enable CSRF protection by default in production"
```

---

## Chunk 3: Implement Mobile Holdings Page

**Spec reference:** `docs/superpowers/specs/2026-08-17-project-analysis-design.md` - Section "FEAT-001: 移动端持仓页缺失"

### Task 3.1: Create HoldingsMobile.vue

**Files:**
- Create: `frontend/src/views-mobile/HoldingsMobile.vue`

- [ ] **Step 1: Study existing mobile view pattern**

```bash
head -50 frontend/src/views-mobile/DashboardMobile.vue
```

- [ ] **Step 2: Create HoldingsMobile.vue**

Create file with:
- Mobile layout using `<el-card>` for each holding
- Show: asset name, quantity, cost basis, current value, PnL
- Use existing `Holdings.vue` data structure
- Responsive design for mobile screens

参考 `frontend/src/views/Holdings.vue` 的数据结构和 `frontend/src/views-mobile/DashboardMobile.vue` 的布局模式。

- [ ] **Step 3: Register route in mobile router**

```bash
cat frontend/src/router/mobile.ts
```

Add holdings route if not present.

- [ ] **Step 4: Build frontend to verify**

```bash
cd frontend && npm run build:mobile
```

Expected: Build succeeds with no errors

- [ ] **Step 5: Commit**

```bash
git add frontend/src/views-mobile/HoldingsMobile.vue
git add frontend/src/router/mobile.ts
git commit -m "feat(mobile): add holdings page for mobile app"
```

---

## Verification

After all chunks are complete:

```bash
# Full verification
cd backend && cmake --build build --parallel && bash tests/test_link.sh
cd ../frontend && npm run build
```

Expected:
- Backend: `PASS=103 FAIL=0`
- Frontend: Build clean, 0 errors
- Mobile: Build clean, 0 errors

---

## Notes

- **console.error statements**: The 5 `console.error` calls found in Vue views are legitimate error handling (not debug residue). They log API failures in catch blocks and should be kept.
- **Mobile Assets.vue**: The existing `AssetsMobile.vue` (27 lines) is a stub. Future work can expand it, but Holdings is prioritized as it's the most used investment feature.
- **reports.c split**: Deferred to P2. Current 796-line file is manageable.
