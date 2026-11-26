# 移动端写入口补全设计（子项目 A + B）

**项目**：Minefolio 移动端功能完善  
**子项目**：A（资产/分类/账户增删改）+ B（完整交易流水录入）  
**日期**：2026-11-26  
**状态**：设计待批准 → 进入 spec-review

---

## 1. 目标

在 D（离线优先增强）已铺好的传输层上，补齐移动端**写入口**，让 6 张可写业务表真正具备"可离线增删改"能力：

1. **A**：资产、账户、分类、标签的移动新增/编辑/删除。
2. **B**：交易流水的移动新增/编辑/删除，覆盖 `asset_id`、`linked_asset_id`、`category_id`、`transaction_type`、`direction`、`fee` 等完整字段。
3. 所有写入统一走 `offlineApi`，断网落本地队列、联网自动推送，冲突沿用 D 的手动确认流程。

---

## 2. 现状回顾

| 能力 | 现状 | 依据 |
|------|------|------|
| 本地镜像 | 6 张表已落本地 SQLite | `frontend/src/db/schema.ts:13-131` |
| 离线传输层 | `TABLE_BY_PATH` + 正则已覆盖 6 张表，`offline-http.ts` 统一调用 `SyncEngine.enqueue()` | D 已实现并通过 CI |
| 离线读 | 资产/账户/流水/日常收支/首页已从本地库读 | `AssetsMobile.vue:51,133,145`、`TransactionsMobile.vue:28,42`、`DailyExpensesMobile.vue:39,62` |
| 离线写 | **仅日常收支有入口**：`ExpenseQuickSheet.vue:275-276` 经 `offlineApi.post/put` | `offlineApi` 在 views-mobile 中仅此一处 |
| 资产写 | 无；`AssetsMobile.vue` 只有 assets/holdings 分段切换 | `AssetsMobile.vue:5-6` |
| 账户写 | 无；桌面 `AccountDialog.vue` 未接入移动端 | `views/Assets.vue:235,254` |
| 分类/标签写 | 无；`SettingsMobile.vue:206` 仅提示"请前往桌面端管理" | `SettingsMobile.vue:206` |
| 交易写 | 无；`TransactionsMobile.vue:15` 只有"加载更多" | `TransactionsMobile.vue:15` |

**核心结论**：D 解决了"能存、能推、能冲突"，A/B 要解决"用户从哪里写"。

---

## 3. 关键设计约束

### 3.1 写入必须走 `offlineApi`

移动端任何 create/update/delete 都必须调用 `offlineApi.post/put/delete`，不得直接调用 `assetsApi` / `accountsApi` / `categoriesApi` / `tagsApi` / `transactionsApi` 的写方法。参考 `ExpenseQuickSheet.vue:275-276` 的既有用法。

### 3.2 ID 重映射需扩展到分类与标签（D 的遗留缺口）

D 的 `SyncEngine` 默认只重映射 `asset_id` / `account_id`（`src/sync/SyncEngine.ts` 的 `remapAssetRefs` / `remapAccountRefs`）。A/B 上线后，离线创建的分类、标签也会产生本地占位 ID：

- 离线创建分类 C（占位 ID）→ 离线创建资产/交易/日常收支引用 `category_id=C`；推送 C 获得服务端真实 ID 后，**必须**同步更新本地依赖行与后续队列项的 `category_id`。
- 离线创建标签 T（占位 ID）→ 离线创建日常收支时 `payload.tags` 含占位 ID；推送 T 获得真实 ID 后，必须更新后续队列项 `payload.tags` 数组中对应值。
- 离线创建父分类 P（占位 ID）→ 子分类 `parent_id=P` 同样需要重映射。

服务端 create 均返回新 ID：category 见 `backend/src/interfaces/http/controllers/category_controller.c:66`，tag 见 `backend/src/interfaces/http/controllers/tag_controller.c:80`。前端 API 类型标注为 `void` 但运行时响应体含 `id`，按 D 现有的 `res?.id ?? (typeof res === 'number' ? res : null)` 解析即可，无需改 API 类型。

### 3.3 账本隔离

`offlineApi` 写入时由 `SyncEngine.enqueue()` 按 `ledgerId → payload.ledger_id → localStorage['minefolio_active_ledger_id']` 解析并写入队列项 `ledger_id`，`pushLocal` 按入队快照发送 `X-Ledger-Id`（D 已实现）。分类/标签本地表无 `ledger_id` 列，按 D 的约定不携带该头。

### 3.4 冲突与错误

沿用 D：`code:1004` 或特定 4xx 标冲突 → `ConflictList` 手动确认；`1002/1003/2001`/400/422 删除队列项并在设置页提示；429/5xx 保持待推送。A/B 不新增冲突策略。

---

## 4. 架构设计

### 4.1 新增组件

| 组件 | 路径 | 职责 |
|------|------|------|
| AssetSheet | `frontend/src/components/mobile/AssetSheet.vue` | 资产新增/编辑（名称、分类、账户、价值/数量/成本、币种、备注），删除走列表确认 |
| AccountSheet | `frontend/src/components/mobile/AccountSheet.vue` | 账户新增/编辑（名称、机构、账号、币种、图标、排序、归档），删除走列表确认 |
| CategorySheet | `frontend/src/components/mobile/CategorySheet.vue` | 分类新增/编辑（名称、type、asset_type、parent_id、icon、sort_order），删除走列表确认 |
| TagSheet | `frontend/src/components/mobile/TagSheet.vue` | 标签新增/编辑（名称、颜色），删除走列表确认 |
| TransactionSheet | `frontend/src/components/mobile/TransactionSheet.vue` | 交易新增/编辑（全字段），删除走列表确认 |

组件契约统一为**纯 props + 事件**模式：

- Props：`visible: boolean`、`record?: T | null`、`categories: Category[]`、`assets: Asset[]`、`accounts: Account[]`、`tags: Tag[]`
- Events：`update:visible`、`saved`
- 组件内部不直接读 store；分类/资产/账户/标签选项由父级从本地库加载后传入。
- 表单校验在组件内完成，错误信息就地展示；提交成功后 `emit('saved')` 由父级刷新列表。

### 4.2 列表页改造

| 页面 | 改动 |
|------|------|
| `AssetsMobile.vue` | 增加 FAB + 资产分组内编辑/删除按钮；接入 `AssetSheet` / `AccountSheet`；写后 `load()` 重新读本地库 |
| `SettingsMobile.vue` | `goCategories()` 由提示改为跳转新增的 `/m/categories`；分类/标签管理页接入 `CategorySheet` / `TagSheet` |
| `TransactionsMobile.vue` | 增加 FAB + 列表项编辑/删除；接入 `TransactionSheet` |
| `DailyExpensesMobile.vue` | 现有 `ExpenseQuickSheet` 保持；补充标签多选入口（复用 `TagPicker` 数据契约，选项来自本地 tags 表） |

**新增路由**：`/m/categories`（`CategoryManageMobile.vue`，分类树 + 标签 Tab），挂在 `MobileLayout` 的 children 下。

### 4.3 写入调用约定（示例）

```ts
// 新增资产
await offlineApi.post('/assets', payload)
// 编辑资产
await offlineApi.put(`/assets/${id}`, payload)
// 删除资产
await offlineApi.delete(`/assets/${id}`)
// 交易同理：/transactions、/transactions/:id
// 分类/标签同理：/categories、/tags
```

`payload` 按 `types/index.ts` 的 `AssetInput` / `AccountInput` / `CategoryInput` / `TransactionInput` / `{name, color}` 构造；`recordId` 删除时从 URL 取 ID，创建时由 `offline-http.ts:nextLocalId()` 生成占位 ID。

### 4.4 SyncEngine 重映射扩展

在 `SyncEngine` 中新增两个内部重映射函数（与现有三个签名一致，均带 `afterQueueId`）：

- `remapCategoryRefs(oldId, newId, afterQueueId)`：
  - 本地依赖行：`categories.parent_id`、`assets.category_id`、`transactions.category_id`、`daily_expenses.category_id`；
  - 后续队列项 payload 中同名字段等于占位 ID 的替换为真实 ID。
- `remapTagRefs(oldId, newId, afterQueueId)`：
  - 本地 `expense_tags.tag_id`；
  - 后续 `daily_expenses` 队列项 payload 的 `tags` 数组中等于占位 ID 的元素替换为真实 ID。

在 `pushLocal` 的 create 分支按 `table_name` 分发：`assets → remapAssetRefs`、`accounts → remapAccountRefs`、`categories → remapCategoryRefs`、`tags → remapTagRefs`。`RemapHandlers` 接口相应扩展为四个可选回调，默认实现仍内置于引擎。

### 4.5 离线读的一致性

新写入口保存成功后，本地 SQLite 行已由 `SyncEngine.enqueue` 的 `writeLocalRow` 落库；父级 `load()` 重新查询本地库即可立即看到变更，无需等待服务端返回。删除操作设 `__deleted=1`，列表查询已过滤（见各视图 SQL）。

---

## 5. 接口约定

### 5.1 分类管理页

- 路由 `/m/categories`，component `CategoryManageMobile.vue`。
- 两个 Tab：分类树（按 type 分组的扁平列表，支持父子层级缩进）、标签列表。
- 分类删除前需校验：存在子节点或被资产/交易/日常收支引用时，服务端会返回 `code:1004`，按 D 的冲突流程处理；移除本地相应行前提示用户。
- 标签删除同理；`expense_tags` 关联行在本地按 `tag_id` 一并清理。

### 5.2 交易表单字段映射

`TransactionInput` 的 `direction` 字段不直接存在，由 `transaction_type` 推导（`deposit/income/interest → in`，`withdrawal/fee/loss → out`，`transfer_* → neutral`），表单只暴露 `transaction_type`，与 `types/index.ts:118-121` 的枚举一致。`fee > 0` 时服务端自动写入子交易，本地 payload 原样携带。

### 5.3 错误处理

所有 Sheet 的 `save()` 用 `try/catch` 包裹 `offlineApi` 调用：

- 离线写入成功：`offlineApi` 返回 `{offline:true,id}`，关闭 Sheet、提示"已离线保存"、刷新列表；
- 在线写入成功：关闭 Sheet、刷新列表；
- 1001：由 `http.ts` 的移动端钩子跳转 `/m/login`；
- 1004 / 校验错误：复用 D 的冲突与 `pushErrors` 机制（写入口本身不弹服务端错误，因为在线失败时 `offlineApi` 直接抛出，需在 Sheet 内展示 `message`）。

---

## 6. 测试计划

### 6.1 前端单元测试（vitest）

- 各 Sheet 的 props 校验与提交 payload 字段完整性（对照 `AssetInput` 等类型）；
- 离线创建分类/标签 → 服务端返回 ID → 依赖行与后续队列项 `category_id` / `tags` 被重映射，历史队列项不变；
- 删除走 `offlineApi.delete` 且本地置 `__deleted=1`；
- 账本切换后已入队项仍发送原 `X-Ledger-Id`。

### 6.2 前端 E2E（Playwright + 请求拦截）

断网 → 移动端新增资产/账户/分类/标签/交易 → 本地列表可见 → 切前台自动推送 → 服务端数据与本地 ID 一致 → 制造 1004 冲突 → `ConflictList` 手动确认。

### 6.3 回归

`npm test`、`npm run typecheck`、`npm run build:mobile` 全绿；后端 `test_link.sh` 仅回归 API 契约。

---

## 7. 实施顺序

1. **阶段一**：扩展 `SyncEngine` 的 category/tag 重映射并补单测（A/B 的正确性前提）。
2. **阶段二**：新增 5 个 Sheet 组件与 `/m/categories` 页面，接入 AssetsMobile / TransactionsMobile / SettingsMobile。
3. **阶段三**：E2E 与全量回归，逐项提交。

---

## 8. 与后续子项目的关系

- **C（移动端 AI 聊天页）**：独立，可并行；不依赖本子项目。
- **E（移动端特有能力）**：本子项目补齐写入口后，holdings/reports/plans 的"本地优先"改造才有完整数据源。
- **D**：本子项目是 D 的传输层与冲突机制的首个真实使用方，任何 D 的缺陷会在本阶段暴露。
