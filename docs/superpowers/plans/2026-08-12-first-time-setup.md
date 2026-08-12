# 首次部署初始化页面与注册关闭 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a first-time deployment initialization page (`/setup`), seed default categories for the initial administrator account, disable public user registration, and update the login UI.

**Architecture:** Add `GET /api/system/status` and `POST /api/system/setup` endpoints to C backend, disable `/api/auth/register` once initialized, add `/setup` view in Vue 3, update `Login.vue` to remove public registration, and update Vue Router navigation guards.

**Tech Stack:** C23, SQLite, Vue 3, Pinia, Vue Router, Element Plus.

---

### Task 1: Backend System Status & Setup Endpoints (`auth.c` & `main.c`)

**Files:**
- Modify: `backend/src/auth.c:1-152`
- Modify: `backend/src/main.c:48-90`

- [ ] **Step 1: Implement `system_status` handler in `auth.c`**

In `backend/src/auth.c`, add `system_status(csilk_ctx_t* c)`:
Query `SELECT COUNT(*) as count FROM users`. If `count > 0`, `initialized = true`, else `false`. Return JSON `{ initialized, user_count }`.

- [ ] **Step 2: Implement `system_setup` handler in `auth.c`**

In `backend/src/auth.c`, add `system_setup(csilk_ctx_t* c)`:
1. Verify `SELECT COUNT(*) FROM users` == 0. If > 0, return 403 / 1004.
2. Bind JSON `{ username, password }`. Validate length.
3. Start transaction `BEGIN TRANSACTION`.
4. Insert user into `users` table with hashed password. Get new `user_id`.
5. Insert default category template rows into `categories` table:
   - Income: `工资`, `理财收益`, `兼职/副业`, `其他收入`
   - Expense: `餐饮`, `交通`, `居住`, `购物`, `娱乐`, `医疗`, `数码电子`, `其他支出`
   - Transaction: `股票/基金`, `加密货币`, `债券/理财`, `定期存款`
6. `COMMIT` transaction.
7. Generate JWT token and return `{ token, expires_in, user: { id, username } }`.

- [ ] **Step 3: Update `auth_register` in `auth.c`**

In `auth_register()`, check `SELECT COUNT(*) FROM users`. If > 0, return `respond_conflict(c, "系统已完成初始化，禁止公开注册")`.

- [ ] **Step 4: Register routes in `main.c`**

In `backend/src/main.c`:
1. Forward declare `extern void system_status(csilk_ctx_t* c);` and `extern void system_setup(csilk_ctx_t* c);`.
2. Update `jwt_middleware_wrapper()` to exclude `/api/system/status` and `/api/system/setup` from token verification.
3. Register routes:
   - `csilk_get(app, "/api/system/status", system_status);`
   - `csilk_post(app, "/api/system/setup", system_setup);`

- [ ] **Step 5: Compile backend and test status endpoint**

Run: `cmake --build backend/build && ./backend/build/minefolio`
Expected: Compiles cleanly.

- [ ] **Step 6: Commit**

```bash
git add backend/src/auth.c backend/src/main.c
git commit -m "feat(backend): add system status, setup handlers, and disable public registration when initialized"
```

---

### Task 2: Backend Integration Tests Update (`test_link.sh`)

**Files:**
- Modify: `backend/tests/test_link.sh`

- [ ] **Step 1: Update `test_link.sh` initialization flow**

In `backend/tests/test_link.sh`:
1. First verify `GET /api/system/status` returns `initialized: false`.
2. Use `POST /api/system/setup` for initial registration & setup instead of `/api/auth/register`.
3. Verify subsequent `POST /api/auth/register` returns code `1004` (registration forbidden).

- [ ] **Step 2: Run backend integration tests**

Run: `cd backend && ./tests/test_link.sh`
Expected: PASS=18 FAIL=0.

- [ ] **Step 3: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test(backend): update test_link.sh to test setup flow and public registration blocking"
```

---

### Task 3: Frontend System API & Auth Store Updates

**Files:**
- Create: `frontend/src/api/system.ts`
- Modify: `frontend/src/stores/auth.ts`

- [ ] **Step 1: Create `frontend/src/api/system.ts`**

Export `systemApi`:
```ts
import { http } from '@/utils/http'

export interface SystemStatus {
  initialized: boolean
  user_count: number
}

export const systemApi = {
  status: () => http.get<SystemStatus, SystemStatus>('/system/status'),
  setup: (data: { username: string; password: string }) =>
    http.post<{ token: string; user: { id: number; username: string } }, any>('/system/setup', data),
}
```

- [ ] **Step 2: Update `frontend/src/stores/auth.ts`**

Add `initialized` state, `checkSystemStatus()` action, and `setup()` action to `auth.ts`.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/api/system.ts frontend/src/stores/auth.ts
git commit -m "feat(frontend): add systemApi and system status state in auth store"
```

---

### Task 4: Frontend Router & Views (`Setup.vue` & `Login.vue`)

**Files:**
- Create: `frontend/src/views/Setup.vue`
- Modify: `frontend/src/views/Login.vue`
- Modify: `frontend/src/router/index.ts`

- [ ] **Step 1: Update `Login.vue` to remove public registration**

In `frontend/src/views/Login.vue`:
1. Remove `isRegister` state and `switch-mode` toggle button.
2. Form submit button displays "登录系统".

- [ ] **Step 2: Create `frontend/src/views/Setup.vue`**

Create `frontend/src/views/Setup.vue` with glassmorphic dark theme:
- Form fields: Admin Username, Admin Password, Confirm Password.
- On submit: call `auth.setup()`, show success message, redirect to `/dashboard`.

- [ ] **Step 3: Update `router/index.ts` navigation guards**

In `frontend/src/router/index.ts`:
1. Add `/setup` route for `Setup.vue` (`meta: { requiresAuth: false }`).
2. In `router.beforeEach`:
   - Check system status via `auth.checkSystemStatus()`.
   - If `initialized === false`: redirect to `/setup` if not already on `/setup`.
   - If `initialized === true`: redirect to `/login` if on `/setup`.

- [ ] **Step 4: Build frontend and verify zero errors**

Run: `npm --prefix frontend run build`
Expected: 0 errors.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/views/Setup.vue frontend/src/views/Login.vue frontend/src/router/index.ts
git commit -m "feat(frontend): add Setup view, remove public registration from Login, and enforce setup router guard"
```
