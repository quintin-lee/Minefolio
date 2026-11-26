# 移动端写入口补全实现计划 (Mobile Write Entries Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 D 已铺好的离线传输层上，补齐资产、账户、分类、标签、交易的移动端新增/编辑/删除入口，使 6 张可写业务表真正可离线增删改；同时补上分类/标签占位 ID 的重映射，避免离线创建后的外键引用错乱。

**Architecture:**
1. **传输层不变**：所有写入统一调用 `offlineApi.post/put/delete`，由 `SyncEngine.enqueue()` 落本地镜像并入队；在线失败由 Sheet 直接展示服务端消息，离线队列失败才进入 ConflictList / `pushErrors`。
2. **重映射扩展**：`SyncEngine` 在现有 `remapAssetRefs` / `remapAccountRefs` 基础上新增 `remapCategoryRefs`（`categories.parent_id`、`assets/transactions/daily_expenses.category_id`）与 `remapTagRefs`（`expense_tags.tag_id`、后续队列 `payload.tags`），均带 `afterQueueId`，默认实现内置于引擎。
3. **UI 层**：5 个纯 props/事件 Sheet + `/m/categories` 分类管理页；列表页读本地 SQLite，写后立即重查本地库，不等待服务端。

**Tech Stack:** Vue 3, Pinia, TypeScript, sql.js, Vitest, Playwright.

**Spec:** `docs/superpowers/specs/2026-11-26-mobile-write-entries-design.md`（spec-review 已修订）。

## Global Constraints

- 不得在移动端直接调用 `assetsApi`/`accountsApi`/`categoriesApi`/`tagsApi`/`transactionsApi` 的 create/update/delete；写入口一律经 `offlineApi`。
- 所有 ID 重映射函数必须保持三参数签名 `(oldId, newId, afterQueueId)`，只处理当前 create 之后的队列项。
- 不向 `TransactionInput` payload 写入 `direction`；可选字段为 `source_type`。
- `AssetInput.account_id` 的占位 ID 由既有 `remapAccountRefs` 处理，不另造机制。
- 分类/标签 create 的响应体含 `id`（后端 `category_controller.c:66`、`tag_controller.c:80`），前端 API 类型虽标 `void` 仍按 `res?.id` 解析，不改 API 类型。
- 删除为软删：`__deleted=1`，列表 SQL 已过滤；`expense_tags` 随标签删除按 `tag_id` 清理。
- 沿用 D 的错误分类：`1004`/特定 4xx 标冲突；`1002`/`1003`/`2001`/400/422 放弃队列项并进 `pushErrors`；429/5xx 保持待推送。

## Review Focus

1. 离线创建分类 → 引用该分类的资产/交易/日常收支推送时携带服务端真实 `category_id`。
2. 离线创建标签 → 后续日常收支 `payload.tags` 使用真实标签 ID。
3. 在线写失败只在 Sheet 内提示，不产生 ConflictList 项；离线队列失败才进入 ConflictList。
4. 各 Sheet payload 字段与 `types/index.ts` 的 Input 类型一致（尤其 `TransactionInput` 无 `direction`）。
5. 账本切换后，已入队项仍按原 `X-Ledger-Id` 推送。

---

## Task 1: SyncEngine 分类/标签重映射扩展

**Files:**
- Modify: `frontend/src/sync/SyncEngine.ts`
- Modify: `frontend/tests/sync.engine.spec.ts`

**Details:**
1. 新增私有方法 `remapCategoryRefs(oldId, newId, afterQueueId)`：
   - 本地行：`UPDATE categories SET parent_id=? WHERE parent_id=?`、`UPDATE assets SET category_id=? ...`、`UPDATE transactions SET category_id=? ...`、`UPDATE daily_expenses SET category_id=? ...`；
   - 后续 `synced=0 AND id > afterQueueId` 队列项：`table_name IN ('assets','transactions','daily_expenses','categories')`，payload 中 `category_id`/`parent_id` 等于占位 ID 的替换为真实 ID；
   - 末尾 `persist()`。
2. 新增私有方法 `remapTagRefs(oldId, newId, afterQueueId)`：
   - 本地 `UPDATE expense_tags SET tag_id=? WHERE tag_id=?`（处理复合主键冲突时先删后插或忽略冲突，按现有 schema 决定）；
   - 后续 `daily_expenses` 队列项 payload 的 `tags` 数组元素替换；
   - `persist()`。
3. `RemapHandlers` 接口扩展为四个可选回调；构造函数默认实现指向四个内部方法；`pushLocal` create 分支分发 `categories → remapCategoryRefs`、`tags → remapTagRefs`（assets/accounts 保持原样）。
4. 单元测试：离线创建父分类 → 子分类 `parent_id` 与资产 `category_id` 重映射；离线创建标签 → 后续 `daily_expenses.payload.tags` 重映射；早于当前 create 的冲突项 payload 保持不变。

- [ ] **Step 1: 实现 `remapCategoryRefs` / `remapTagRefs` 与四路分发**
- [ ] **Step 2: 补充 `sync.engine.spec.ts` 的三个重映射用例**
- [ ] **Step 3: 运行 `npm test -- tests/sync.engine.spec.ts` 与 `npm run typecheck`**

---

## Task 2: 五个移动端 Sheet 组件

**Files:**
- Create: `frontend/src/components/mobile/AssetSheet.vue`
- Create: `frontend/src/components/mobile/AccountSheet.vue`
- Create: `frontend/src/components/mobile/CategorySheet.vue`
- Create: `frontend/src/components/mobile/TagSheet.vue`
- Create: `frontend/src/components/mobile/TransactionSheet.vue`

**Details:**
1. 统一契约：`visible: boolean`、`record?: T | null`、选项数组（categories/assets/accounts/tags）、`update:visible`、`saved`；内部不读 store。
2. 字段与 payload：
   - AssetSheet：`name`、`category_id`、`account_id`、`current_value`/`quantity`/`cost_basis`/`net_value`、`currency`、`note`（`AssetInput`）；
   - AccountSheet：`name`、`institution`、`account_no`、`currency`、`icon`、`note`、`sort_order`（`AccountInput`/`AccountUpdate`）；
   - CategorySheet：`name`、`type`、`asset_type`、`currency`、`parent_id`、`icon`、`sort_order`（`CategoryInput`）；
   - TagSheet：`name`、`color`；
   - TransactionSheet：`asset_id`、`linked_asset_id`、`category_id`、`transaction_type`、`amount`、`price_per_unit`、`quantity`、`fee`、`currency`、`transaction_date`、`note`、`source_type`；**不写 `direction`**。
3. 保存逻辑统一为：
   ```ts
   if (props.record) await offlineApi.put(`${base}/${props.record.id}`, payload)
   else await offlineApi.post(base, payload)
   ```
   `base` 分别为 `/assets`、`/accounts`、`/categories`、`/tags`、`/transactions`。
4. 在线失败在组件内展示 `err.response.data.message` 并保持表单；离线成功返回 `{offline:true,id}`，关闭并 `emit('saved')`。

- [ ] **Step 1: 新建五个 Sheet 组件与表单校验**
- [ ] **Step 2: 接入 `offlineApi` 写调用与在线/离线分支**
- [ ] **Step 3: 补组件单测（payload 字段完整性、在线失败不关闭表单）**

---

## Task 3: 分类管理页与路由

**Files:**
- Create: `frontend/src/views-mobile/CategoryManageMobile.vue`
- Modify: `frontend/src/router/mobile.ts`

**Details:**
1. `/m/categories` 挂在 `MobileLayout` children 下，component `CategoryManageMobile.vue`。
2. 页面两个 Tab：分类树（按 `type` 分组、按 `parent_id` 缩进）与标签列表；分类选项从本地 `categories` 表读（`__deleted=0`），标签从本地 `tags` 表读。
3. 接入 `CategorySheet` / `TagSheet`；删除前 `ElMessageBox.confirm`，调用 `offlineApi.delete('/categories/:id')` / `'/tags/:id'`；本地 `expense_tags` 按 `tag_id` 清理。
4. `SettingsMobile.vue:206` 的 `goCategories()` 改为 `router.push('/m/categories')`。

- [ ] **Step 1: 新建 `CategoryManageMobile.vue` 并接入两个 Sheet**
- [ ] **Step 2: 添加 `/m/categories` 路由并改 `goCategories()`**
- [ ] **Step 3: `npm run typecheck` 与移动端布局测试**

---

## Task 4: 列表页接入写入口

**Files:**
- Modify: `frontend/src/views-mobile/AssetsMobile.vue`
- Modify: `frontend/src/views-mobile/TransactionsMobile.vue`
- Modify: `frontend/src/views-mobile/DailyExpensesMobile.vue`

**Details:**
1. `AssetsMobile.vue`：增加 FAB（新增资产）与账户分组的编辑/删除按钮，资产分组内增加资产编辑/删除；接入 `AssetSheet` / `AccountSheet`；`saved` 后重新 `load()` 读本地库。
2. `TransactionsMobile.vue`：增加 FAB 与列表项编辑/删除；接入 `TransactionSheet`；`saved` 后重查本地库。
3. `DailyExpensesMobile.vue` / `ExpenseQuickSheet.vue`：补充标签多选入口，选项来自本地 `tags` 表，提交时 tags ID 放入 `payload.tags`（沿用 D 约定的服务端原子写入）。
4. 所有删除走 `offlineApi.delete`，本地由 `SyncEngine.writeLocalRow` 置 `__deleted=1`。

- [ ] **Step 1: AssetsMobile 接入资产/账户写入口**
- [ ] **Step 2: TransactionsMobile 接入交易写入口**
- [ ] **Step 3: 日常收支接入本地标签选项**
- [ ] **Step 4: `npm run typecheck`**

---

## Task 5: 测试与端到端验证

**Files:**
- Modify: `frontend/tests/sync.engine.spec.ts`
- Create: `frontend/e2e/mobile-write-entries.spec.ts`

**Details:**
1. 单测：五类 Sheet 的 payload 字段完整性；分类/标签重映射边界（Task 1）；在线失败保留表单；离线成功刷新本地列表。
2. E2E（Playwright + 请求拦截）：断网 → 移动端新增资产/账户/分类/标签/交易 → 本地列表可见 → 切前台自动推送 → 服务端数据与本地 ID 一致 → 制造 `code:1004` → ConflictList 手动确认。
3. 回归：`npm test`、`npm run typecheck`、`npm run build:mobile`；后端 `test_link.sh` 仅回归 API 契约。

- [ ] **Step 1: 补充组件与列表页单测**
- [ ] **Step 2: 编写并运行 E2E**
- [ ] **Step 3: 全量测试与移动端构建通过**

---

## Verification Checklist

- [ ] 6 张表写入口均经 `offlineApi`，无直接 `http` 写调用
- [ ] 分类/标签占位 ID 重映射正确，历史队列项不被误改
- [ ] 在线写失败只在 Sheet 提示；离线队列失败才进 ConflictList/pushErrors
- [ ] `TransactionInput` payload 无 `direction` 字段
- [ ] 账本切换后已入队项仍按原 `X-Ledger-Id` 推送
- [ ] `npm test` / `npm run typecheck` / `npm run build:mobile` 全绿
