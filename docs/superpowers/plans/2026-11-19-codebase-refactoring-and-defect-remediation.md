# Minefolio 全面审查与存量缺陷重构实施计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 根除后端报表服务堆内存与 JSON 泄漏（DEF-01）、下沉控制器残留 SQL 至领域仓储（DEF-02）、修正资产组合权重多币种与账本计算缺陷（DEF-03）、加固移动端本地 SQLite 多账本隔离与向下兼容迁移（DEF-04）、重构移动端离线双向同步定向请求头与维表拉取（DEF-05）、统一 Pinia 响应式状态失效与移动视图过滤（DEF-06），并在受限隔离环境内完成全量回归验证（DEF-07）。

**Architecture:** 
- **阶段一（后端稳固）**：在 C23 报表聚合服务中显式回收所有 `csilk_json_t*` 查询结果集与 `calloc` 堆内存，杜绝 ASan 泄漏；将 `ai_controller.c` 与 `im_bot_controller.c` 中的原生 SQL 下沉封装进基础设施仓储；在资产组合权重聚合中引入 `exchange_rate_convert_m` 统一度量衡至本位币（CNY）。
- **阶段二（端侧加固）**：为移动端 sql.js WASM 本地表（`assets`、`transactions`、`daily_expenses`、`sync_queue`）全量补齐 `ledger_id` 列并完成本地幂等补列迁移；改造 `sync.ts`，推送队列项时按单条记录的 `ledger_id` 定向注入 `X-Ledger-Id` 请求头，拉取时增补 `categories` 与 `tags`；为移动端离线查询加上 `ledger_id` 过滤。
- **强制约束（隔离与内存限额）**：所有编译和测试统一通过 `./backend/tests/run_isolated.sh` 与 `./backend/tests/memory_guard.sh` 执行，锁定 cgroups v2、限制 `MemoryMax`、关闭 swap，配置 ASan 硬上限 `hard_rss_limit_mb=2048`。

**Tech Stack:** C23, SQLite, PostgreSQL, csilk HTTP framework, Vue 3, Pinia, TypeScript, sql.js (WASM SQLite).

**Spec:** [docs/superpowers/specs/2026-11-19-codebase-refactoring-and-defect-remediation-design.md](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-19-codebase-refactoring-and-defect-remediation-design.md)

## Global Constraints

- 所有后端构建必须使用隔离限流指令：`./backend/tests/run_isolated.sh build`（限制 MemoryMax=6G, 并发 <= 4）。
- 所有后端 CTest 必须使用隔离限流指令：`./backend/tests/run_isolated.sh unit`（限制 MemoryMax=4G, ASan RSS <= 2048MB）。
- 所有后端集成测试必须使用隔离限流指令：`./backend/tests/run_isolated.sh integration`（限制 MemoryMax=2G, 顺序执行）。
- 所有前端构建与测试必须通过 `memory_guard.sh` 封顶虚拟内存：`source ./backend/tests/memory_guard.sh && run_mem_capped 4G ...`。
- 所有 C 源码必须在 `-Werror` 下保持零编译警告。
- 严格保证 SQLite 与 PostgreSQL 双引擎行为绝对一致。
- 遵循 DDD 四层架构规范，严禁在 `interfaces/http/controllers/` 出现任何 SQL 字符串或直接 DB 查询。

## Review Focus

1. **报表高频查询无泄漏**：用户多次请求资产走势与分类汇总报表，ASan（LeakSanitizer）报告 0 byte 堆泄漏与 0 个未回收 JSON 节点。
2. **控制器完全解耦 SQL**：`grep -rn "SELECT \|UPDATE \|INSERT \|DELETE " backend/src/interfaces/http/controllers/` 输出必须为空。
3. **多币种多账本权重准确**：同一账本内包含 100 USD 美股和 700 CNY 现金时，组合权重必须经汇率折算后计算，而不是得到荒谬的裸数值累加比例；资产无合法账本时必须回退到默认账本。
4. **离线跨账本记账不串写**：移动端在离线状态下向“账本 A”记账，连网前手动切换至“账本 B”，触发同步后，该条记录在服务端必须精准写入“账本 A”而非被篡改推入“账本 B”。
5. **离线分类与标签不白屏**：移动端离线拉取后，本地本地库拥有完整的 `categories` 与 `tags` 数据，离线新建支出时可选分类和图标。

---

### Task 1: 报表聚合服务内存泄漏根治 (DEF-01)

**Files:**
- Modify: `backend/src/services/report/report_asset_breakdown.c:30-70, 175-185, 375-405, 500-510`
- Modify: `backend/src/services/report/report_asset_trend.c:90-105, 140-155, 215-225`
- Test: `backend/tests/unit/test_report_services.c`

**Interfaces:**
- Consumes: `csilk_json_free(csilk_json_t*)`, `free(void*)`
- Produces: 零内存泄漏的 `report_asset_breakdown()`, `report_asset_currency_breakdown()`, `report_asset_trend()`

- [ ] **Step 1: 检查现有报表单元测试在 ASan 环境下的泄漏基线**

运行隔离单元测试检查当前测试状态：
```bash
./backend/tests/run_isolated.sh unit backend/build-asan
```

- [ ] **Step 2: 修复 `report_asset_breakdown.c` 中的 3 处 JSON 查询泄漏**
  - 在 `report_asset_breakdown()`（约第 180 行）计算完成后、函数返回前添加：
    ```c
    if (assets) {
        csilk_json_free(assets);
    }
    if (liabs) {
        csilk_json_free(liabs);
    }
    ```
  - 在 `report_asset_currency_breakdown()`（约第 506 行）响应装配完毕后添加：
    ```c
    if (rows) {
        csilk_json_free(rows);
    }
    ```

- [ ] **Step 3: 修复 `report_asset_trend.c` 中的 JSON 结果集与堆内存泄漏**
  - 在 `report_asset_trend()`（约第 150 行）日志分析前或统一退出位置添加：
    ```c
    if (asset_rows) {
        csilk_json_free(asset_rows);
    }
    ```
  - 在函数清理阶段（约第 218 行）`if (logs) free(logs);` 之后添加：
    ```c
    if (assets) {
        free(assets);
    }
    ```

- [ ] **Step 4: 隔离编译并在 ASan 下运行 CTest 验证零泄漏**

运行：
```bash
./backend/tests/run_isolated.sh build backend/build-asan
./backend/tests/run_isolated.sh -- ./backend/build-asan/test_report_services
```
预期：测试通过，AddressSanitizer 与 LeakSanitizer 报告 0 errors / 0 leaks。

- [ ] **Step 5: 提交修复**

```bash
git add backend/src/services/report/report_asset_breakdown.c backend/src/services/report/report_asset_trend.c
git commit -m "fix(report): free json query results and heap buffers to prevent memory leaks"
```

---

### Task 2: DDD 分层治理 — 控制器层残留 SQL 下沉至仓储层 (DEF-02)

**Files:**
- Modify: `backend/src/infrastructure/repositories/ai_repo_impl.h`
- Modify: `backend/src/infrastructure/repositories/ai_repo_impl.c`
- Modify: `backend/src/interfaces/http/controllers/ai_controller.c:360-405`
- Modify: `backend/src/interfaces/http/controllers/im_bot_controller.c:70-86`
- Test: `backend/tests/unit/test_ai_action_confirm.c`, `backend/tests/test_im_bot.sh`

**Interfaces:**
- Consumes: `mf_auth_repo_get_by_id(void* pool, int64_t user_id, mf_user_t* out_user)`
- Produces: `int mf_ai_repo_mark_draft_executed(void* pool, int64_t user_id, const char* draft_token)`

- [ ] **Step 1: 在仓储接口中声明并实现 `mf_ai_repo_mark_draft_executed`**
  - 在 `backend/src/infrastructure/repositories/ai_repo_impl.h` 增加声明：
    ```c
    int mf_ai_repo_mark_draft_executed(void* pool, int64_t user_id, const char* draft_token);
    ```
  - 在 `backend/src/infrastructure/repositories/ai_repo_impl.c` 中实现：
    - 查询包含 `draft_token` 的消息记录；
    - 调用 `ai_mark_content_executed()` 与 `ai_mark_tool_calls_executed()`；
    - 执行 `UPDATE ai_messages SET content = ?, tool_calls = ? WHERE id = ?` 并清理临时字符串。

- [ ] **Step 2: 改造 `ai_controller.c`，使用仓储方法替换内联 SQL**
  - 在 `backend/src/interfaces/http/controllers/ai_controller.c` 中：
    - 将 `mark_message_draft_executed()` 函数体直接委托给 `mf_ai_repo_mark_draft_executed(pool, user_id, draft_token)`；
    - 移除控制器内的 `SELECT` 与 `UPDATE` SQL。

- [ ] **Step 3: 改造 `im_bot_controller.c`，使用领域仓储获取 `token_version`**
  - 在 `backend/src/interfaces/http/controllers/im_bot_controller.c`（约第 75 行）：
    - 引入 `domain/auth/repository.h`；
    - 替换原有的 `csilk_db_query_param_json(..., "SELECT token_version FROM users...")` 为：
      ```c
      mf_user_t user = {0};
      if (mf_auth_repo_get_by_id(db_get_pool(), user_id, &user) == 0) {
          token_version = (int)user.token_version;
      }
      ```

- [ ] **Step 4: 静态规则检查与单元测试验证**

运行控制台静态检查，确保无任何 SQL 残留：
```bash
grep -rn "SELECT \|UPDATE \|INSERT \|DELETE " backend/src/interfaces/http/controllers/
```
预期：除注释外无任何匹配行。

在隔离环境编译并运行相关测试：
```bash
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh -- ./backend/build/test_ai_action_confirm
```
预期：PASS。

- [ ] **Step 5: 提交修复**

```bash
git add backend/src/infrastructure/repositories/ai_repo_impl.h \
        backend/src/infrastructure/repositories/ai_repo_impl.c \
        backend/src/interfaces/http/controllers/ai_controller.c \
        backend/src/interfaces/http/controllers/im_bot_controller.c
git commit -m "refactor(arch): sink inline SQL from controllers to repository layer"
```

---

### Task 3: 资产组合权重多币种折算与账本作用域修正 (DEF-03)

**Files:**
- Modify: `backend/src/application/asset/usecases.c:585-625`
- Test: `backend/tests/unit/test_domain_portfolio.c`

**Interfaces:**
- Consumes: `exchange_rate_convert_m(money_t, currency_t)`, `percentage_calc()`
- Produces: 币种归一化的高精度组合权重计算

- [ ] **Step 1: 编写多币种持仓组合权重单测**
  - 在 `backend/tests/unit/test_domain_portfolio.c` 中添加针对多币种资产（例如 100 USD 资产与 700 CNY 现金）的权重比率校验用例，断言权重符合汇率折算结果。

- [ ] **Step 2: 改造 `application/asset/usecases.c` 中的组合权重计算**
  - 强制账本约束：当 `ledger_id <= 0` 时，通过 `ledger_get_default(db_pool, user_id)` 获取默认账本 ID，杜绝跨账本全局扫描；
  - 币种归一化：
    - 读取资产币种，将每个资产的市值 `current_value` 通过 `exchange_rate_convert_m()` 折算为账本本位币（`CNY`）；
    - 将资产自身的 `self_val` 同样通过 `exchange_rate_convert_m()` 折算为本位币；
    - 使用 `percentage_calc(self_val_base, total_mkt_base, ...)` 计算最终权重百分比。

- [ ] **Step 3: 隔离环境编译并运行测试**

```bash
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh unit backend/build
```
预期：CTest 单元测试全部 PASS。

- [ ] **Step 4: 提交代码**

```bash
git add backend/src/application/asset/usecases.c backend/tests/unit/test_domain_portfolio.c
git commit -m "fix(asset): normalize multi-currency valuations to base currency in portfolio weight"
```

---

### Task 4: 移动端本地 SQLite Schema 与迁移加固 (DEF-04)

**Files:**
- Modify: `frontend/src/db/schema.ts:22-125`
- Modify: `frontend/src/db/local.ts:75-90`
- Test: `frontend/tests/db.local.spec.ts`

**Interfaces:**
- Consumes: `sql.js`
- Produces: 包含 `ledger_id` 列的本地数据库表结构及旧库向下兼容补列能力

- [ ] **Step 1: 编写本地数据库迁移单测**
  - 在 `frontend/tests/db.local.spec.ts` 中添加用例：验证已有无 `ledger_id` 的旧数据库能够通过 `migrateColumns()` 成功补齐 `ledger_id` 列，且表数据不损坏。

- [ ] **Step 2: 运行测试验证失败**

运行：
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend test tests/db.local.spec.ts
```
预期：FAIL（缺少补列迁移）。

- [ ] **Step 3: 更新 `frontend/src/db/schema.ts`**
  - 在 `assets` 表中增加：`ledger_id INTEGER DEFAULT 0`
  - 在 `transactions` 表中增加：`ledger_id INTEGER DEFAULT 0`
  - 在 `daily_expenses` 表中增加：`ledger_id INTEGER DEFAULT 0`
  - 在 `sync_queue` 表中增加：`ledger_id INTEGER DEFAULT 0`

- [ ] **Step 4: 更新 `frontend/src/db/local.ts` 补列迁移**
  - 在 `migrateColumns()` 中增加：
    ```ts
    'ALTER TABLE assets ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE transactions ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE daily_expenses ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE sync_queue ADD COLUMN ledger_id INTEGER DEFAULT 0',
    ```

- [ ] **Step 5: 重新运行测试验证通过**

运行：
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend test tests/db.local.spec.ts
```
预期：PASS。

- [ ] **Step 6: 提交代码**

```bash
git add frontend/src/db/schema.ts frontend/src/db/local.ts frontend/tests/db.local.spec.ts
git commit -m "feat(mobile): add ledger_id column to local schema and migration for offline isolation"
```

---

### Task 5: 移动端离线数据双向同步与定向请求头加固 (DEF-05)

**Files:**
- Modify: `frontend/src/stores/sync.ts`
- Modify: `frontend/src/api/daily_expenses.ts`
- Modify: `frontend/src/api/transactions.ts`
- Modify: `frontend/src/api/assets.ts`
- Modify: `frontend/src/api/accounts.ts`
- Test: `frontend/tests/sync.store.spec.ts`

**Interfaces:**
- Consumes: `API_BY_TABLE`, `sync_queue`
- Produces: 具有账本定向注入的 `pushLocal()` 与覆盖完整维表的 `pullRemote()`

- [x] **Step 1: 编写同步队列多账本隔离测试用例**
  - 在 `frontend/tests/sync.store.spec.ts` 中添加用例：
    - 断言 `enqueue` 能够写入 `ledger_id`；
    - 断言 `pushLocal` 时向 API 传递带 `X-Ledger-Id` 覆盖的 headers，而不是使用全局当前激活的账本；
    - 断言 `pullRemote` 会拉取 `categories` 与 `tags`。

- [x] **Step 2: 运行测试验证失败**

运行：
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend test tests/sync.store.spec.ts
```
预期：FAIL。

- [x] **Step 3: 更新 API 模块支持动态请求头**
  - 在 `frontend/src/api/daily_expenses.ts`、`transactions.ts`、`assets.ts`、`accounts.ts` 的 `create`、`update`、`delete` 方法中支持可选的 `config?: AxiosRequestConfig` 参数，允许透传 headers。

- [x] **Step 4: 改造 `frontend/src/stores/sync.ts`**
  - **白名单维护**：在 `TABLE_COLUMNS` 中为 `assets`、`transactions`、`daily_expenses` 增加 `'ledger_id'`；
  - **入队绑定账本**：在 `enqueue(tableName, recordId, operation, payload)` 中读取当前活跃 `ledger_id` 并存入 `sync_queue.ledger_id`；
  - **定向注入请求头**：在 `pushLocal()` 中，如果 `item.ledger_id > 0`，调用 API 时传入 `{ headers: { 'X-Ledger-Id': String(item.ledger_id) } }`；
  - **拉取维表补全**：更新 `pullRemote()` 中的拉取表清单为：
    ```ts
    const tables = ['accounts', 'categories', 'tags', 'assets', 'transactions', 'daily_expenses']
    ```
  - **并发锁与防抖**：在 `syncNow()` 增加原子标记检查与异常安全释放。

- [x] **Step 5: 运行单测验证通过**

运行：
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend test tests/sync.store.spec.ts
```
预期：PASS。

- [x] **Step 6: 提交代码**

```bash
git add frontend/src/stores/sync.ts \
        frontend/src/api/daily_expenses.ts \
        frontend/src/api/transactions.ts \
        frontend/src/api/assets.ts \
        frontend/src/api/accounts.ts \
        frontend/tests/sync.store.spec.ts
git commit -m "fix(mobile): enforce ledger_id header injection and include categories in sync"
```

---

### Task 6: 移动端离线视图账本过滤与 Pinia 状态流转治理 (DEF-06)

**Files:**
- Modify: `frontend/src/stores/ledger.ts:70-85`
- Modify: `frontend/src/views/Layout.vue:200-206`
- Modify: `frontend/src/views-mobile/MobileLayout.vue:40-46`
- Modify: `frontend/src/views-mobile/TransactionsMobile.vue:30-45`
- Modify: `frontend/src/views-mobile/DailyExpensesMobile.vue:55-70`
- Modify: `frontend/src/views-mobile/ExpenseQuickSheet.vue:60-75`

**Interfaces:**
- Consumes: `useLedgerStore`, `useCategoryStore`, `useSyncStore`
- Produces: 账本作用域隔离的离线移动端视图与可控的状态失效

- [x] **Step 1: 改造移动端离线列表 SQL 查询**
  - 在 `TransactionsMobile.vue` 的离线查询中加入账本条件：
    ```sql
    WHERE t.__deleted = 0 AND (t.ledger_id = ? OR t.ledger_id = 0)
    ```
    参数传入当前 `currentLedgerId`；
  - 在 `DailyExpensesMobile.vue` 的 `loadLocalList()` 与 `loadLocalMonth()` 中加入相同账本约束；
  - 在 `ExpenseQuickSheet.vue` 中确保录入的离线记录写入 `ledger_id = currentLedgerId`。

- [x] **Step 2: 规范 Pinia `ledger.ts` 切换响应**
  - 在 `useLedgerStore` 中提供统一的响应式切账本调度：
    - 保留对旧事件的兼容，同时提供直接的 store 级联失效；
    - 在移动端触发当前账本重新加载与局部刷新。

- [x] **Step 3: 前端类型检查与构建验证**

运行：
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend run build
```
预期：`vue-tsc -b` 0 错误，Vite 构建产物 0 告警顺利打包。

- [x] **Step 4: 提交代码**

```bash
git add frontend/src/stores/ledger.ts \
        frontend/src/views/Layout.vue \
        frontend/src/views-mobile/MobileLayout.vue \
        frontend/src/views-mobile/TransactionsMobile.vue \
        frontend/src/views-mobile/DailyExpensesMobile.vue \
        frontend/src/views-mobile/ExpenseQuickSheet.vue
git commit -m "refactor(frontend): filter mobile local queries by active ledger and streamline store invalidation"
```

---

### Task 7: 隔离全量编译与双引擎门禁验证 (DEF-07)

**Files:**
- Test: All unit and integration test suites

- [x] **Step 1: 隔离环境全量后端构建**
```bash
./backend/tests/run_isolated.sh build backend/build
./backend/tests/run_isolated.sh build backend/build-asan
```
预期：零编译错误，`-Werror` 零告警。

- [x] **Step 2: 隔离环境运行后端全量 CTest 单元测试 (包含 ASan 零泄漏)**
```bash
./backend/tests/run_isolated.sh unit backend/build-asan
```
预期：55+ 单元测试全部 PASS，ASan 零泄漏。

- [x] **Step 3: 隔离环境运行全套集成测试 (SQLite)**
```bash
./backend/tests/run_isolated.sh integration backend/build
```
预期：15 个集成测试套件（含 `test_link.sh`, `test_ledgers.sh`, `test_ai_trace.sh` 等）100% 绿色通过。

- [x] **Step 4: 隔离环境运行前端测试与构建**
```bash
source ./backend/tests/memory_guard.sh
run_mem_capped 4G npm --prefix frontend test
run_mem_capped 4G npm --prefix frontend run build
```
预期：Vitest 40+ 测试套件全绿，打包顺利完成。

- [x] **Step 5: 提交最终文档与改动收尾**
```bash
git status
```
预期：工作区干净，无未追踪文件与遗留产物。
