# Minefolio 双架构绞杀第一阶段：建立 test_full.sh 门禁与 Market 领域绞杀实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对齐并彻底修复全量功能测试门禁 `test_full.sh`（达到 100% 通过），并绞杀下线遗留 `market` 领域代码（删除 `controllers/market_controller.*` 与 `services/market_service.*`），将路由与后台调度直接接入 DDD 架构层。

**Architecture:** 采用绞杀者模式（Strangler Fig Pattern）。首先修复端到端门禁脚本中的假失败（jq empty 陷阱、单引号内转义错误、分类名与默认数据冲突、复数路径端点对齐、浮点数值比较容差）；其次将 `main.c` 和 `market_scheduler.c` 分别切换至 DDD 层的 `interfaces/http/controllers/market_controller.h` 与 `application/market/usecases.h`；最后安全物理移除遗留空壳 Facade 文件，并通过全套自动化门禁。

**Tech Stack:** C23, Csilk Framework, SQLite3, Bash, jq, Node.js crypto, CMake.

---

### Task 1: 修复 `test_full.sh` 基础工具函数与系统/分类/标签测试用例

**Files:**
- Modify: `backend/tests/test_full.sh:19-198`

- [ ] **Step 1: 修复 `extract_data` 避免 `false // empty` 吞噬布尔值，并新增 `check_num` 浮点数容差断言函数**

在 `backend/tests/test_full.sh` 顶部：
1. 增强 `check_num` 函数，支持浮点与整数的绝对值容差（0.001）比较：
```bash
check_num() {
  local desc="$1" expected="$2" actual="$3"
  local diff=$(awk -v e="$expected" -v a="$actual" 'BEGIN { d = e - a; if (d < 0) d = -d; print (d < 0.001) ? "1" : "0" }' 2>/dev/null || echo "0")
  if [ "$diff" = "1" ]; then
    PASS=$((PASS+1)); echo "  ✅ $desc"
  else
    FAIL=$((FAIL+1)); FAILED_CASES+=("$desc (期望 $expected 实际 $actual)")
    echo "  ❌ $desc (期望 $expected 实际 $actual)"
  fi
}
```
2. 修复 `extract_data`：
```bash
extract_data() {
  if [ -n "${1:-}" ] && [ "${1:0:1}" = "." ]; then
    { jq -r "if .data$1 != null then .data$1 else empty end" 2>/dev/null; } || echo ""
  else
    { echo "${1:-}" | jq -r "if .data${2:-} != null then .data${2:-} else empty end" 2>/dev/null; } || echo ""
  fi
}
```

- [ ] **Step 2: 修复 Section B 分类初始化测试与分类更新、标签更新**

1. Section B 针对已有预设分类进行正确断言或查询。由于 `system_setup` 会自动初始化内置分类体系，创建测试专用分类前缀 `FT-`（如 `FT-餐饮`、`FT-工资` 等），或在已有分类存在时直接通过 `id_by_name` 取出 ID：
```bash
for nm in "FT-餐饮:expense" "FT-工资:income" "FT-现金账户:asset" "FT-银行卡:asset" "FT-信用卡:asset" "FT-股票:asset" "FT-基金:asset" "FT-债券:asset" "FT-加密货币:asset" "FT-贷款:asset" "FT-其他负债:asset"; do
...
```
更新对应的 `EXPENSE_CAT`、`INCOME_CAT`、`CASH_CAT`、`BANK_CAT` 等提取逻辑，确保 ID 提取成功（非空）。
2. 修复 Section C 标签更新单引号内多余反斜杠转义问题：
```bash
# 将 '{\"name\":\"商务出差\",\"color\":\"#ef4444\"}' 修正为合法 JSON：
UPD_TAG=$(extract_code "$(req_auth PUT /tags/$T1 '{"name":"商务出差","color":"#ef4444"}' "$TOKEN")")
```
3. 分类总数断言：断言 `TOTAL_CAT > 0` 且包含刚建的测试分类，避免对硬编码的绝对数字 12 做脆弱匹配。

- [ ] **Step 3: 运行前三节测试并验证**

Run: `bash -c "DB=/tmp/mf_t1.db; cd backend && TEST_PORT=8182 ./tests/test_full.sh | head -n 45"`
Expected: Section A (系统初始化与认证)、Section B (分类管理)、Section C (标签管理) 全部断言显示 ✅。

- [ ] **Step 4: 提交 Task 1 修改**

```bash
git add backend/tests/test_full.sh
git commit -m "test(gate): 🎯 fix extract_data, check_num, and category/tag test assertions in test_full.sh"
```

---

### Task 2: 修复 `test_full.sh` 资产、记账、持仓、现金流、DCA 与 CSV 测试用例

**Files:**
- Modify: `backend/tests/test_full.sh:199-540`

- [ ] **Step 1: 修复资产与记账流程中的数值比较**

1. 将资产、余额、净值校验从纯字符串 `check` 替换为 `check_num`：
   - 钱包余额 `10000` vs `10000.0`
   - 存款/取款/收入后余额校验
   - 更新差量与删除反转校验
   - AAPL 净值更新后 current_value 校验
2. 修复更新资产时 JSON 字符串转义：
```bash
UPD_ASSET=$(extract_code "$(req_auth PUT /assets/$WALLET "{\"name\":\"我的钱包\",\"category_id\":$CASH_CAT,\"current_value\":12000,\"currency\":\"CNY\"}" "$TOKEN")")
```
确保 `$CASH_CAT` 和 `$WALLET` 非空。

- [ ] **Step 2: 修复 Section K 现金流计划路径与字段**

1. 将 POST `/cashflow/schedule` 修正为复数形式 `/cashflow/schedules`。
2. 字段对齐后端 DTO：
```bash
CF1=$(extract_code "$(req_auth POST /cashflow/schedules "{\"name\":\"月度工资\",\"expected_amount\":15000,\"flow_type\":\"income\",\"category_id\":$INCOME_CAT,\"source_asset_id\":$BANK,\"target_asset_id\":$WALLET,\"frequency\":\"monthly\",\"start_date\":\"2026-09-01\"}" "$TOKEN")")
```
3. 相应更新 GET、PUT、DELETE 现金流端点路径为 `/cashflow/schedules`。

- [ ] **Step 3: 修复 Section L DCA 定投计划字段与执行**

1. 修正创建 DCA 计划请求体参数名：
   - `asset_id` -> `target_asset_id`
   - `linked_asset_id` -> `funding_asset_id`
```bash
DCA1=$(extract_code "$(req_auth POST /dca/plans "{\"name\":\"AAPL月定投\",\"target_asset_id\":$STOCK,\"funding_asset_id\":$WALLET,\"amount\":300,\"frequency\":\"monthly\",\"day_of_month\":15,\"start_date\":\"2026-09-15\"}" "$TOKEN")")
```
2. 执行 DCA 测试对齐 `test_dca_cashflow.sh` 的正确执行端点与流程。

- [ ] **Step 4: 修复 Section M CSV 导出及 Section O 边界测试**

1. 确保 CSV 导出端点对齐（`/api/transactions/export`, `/api/daily-expenses/export`）。
2. 边界测试中的百万级交易金额测试确保入参有效。

- [ ] **Step 5: 运行完整 `test_full.sh` 并验证 100% 通过**

Run: `cd backend && ./tests/test_full.sh`
Expected:
```text
================================================================
  完整功能测试结果
  PASS=153+  FAIL=0
================================================================
```
Exit code 0.

- [ ] **Step 6: 提交 Task 2 修改**

```bash
git add backend/tests/test_full.sh
git commit -m "test(gate): 🎯 align asset, cashflow, dca, and csv assertions in test_full.sh"
```

---

### Task 3: 切换 `market_scheduler.c` 与 `main.c` 依赖至 DDD 架构层

**Files:**
- Modify: `backend/src/main.c:23`
- Modify: `backend/src/services/market/market_scheduler.c:2,225-235`

- [ ] **Step 1: 修改 `backend/src/main.c`**

将第 23 行的旧引入：
```c
#include "controllers/market_controller.h"
```
替换为 DDD 接口层头文件：
```c
#include "interfaces/http/controllers/market_controller.h"
```

- [ ] **Step 2: 修改 `backend/src/services/market/market_scheduler.c`**

1. 将包含头文件从 `services/market_service.h` 改为 `application/market/usecases.h`：
```c
#include "application/market/usecases.h"
```
2. 将第 229 行函数调用从 `market_service_do_sync_user` 改为 `market_usecase_do_sync_user`：
```c
            market_usecase_do_sync_user(g_pool, 0, &synced, &failed);
```

- [ ] **Step 3: 编译后端并验证语法与链接正确**

Run: `cmake --build backend/build --parallel`
Expected: 编译通过，0 错误，0 警告。

- [ ] **Step 4: 运行 `test_market_sync.sh` 验证功能完好**

Run: `cd backend && ./tests/test_market_sync.sh`
Expected: 18 个测试用例全部通过。

- [ ] **Step 5: 提交 Task 3 修改**

```bash
git add backend/src/main.c backend/src/services/market/market_scheduler.c
git commit -m "refactor(market): ♻️ rewire main and scheduler to DDD interfaces and usecases"
```

---

### Task 4: 物理删除遗留 Market Facade 文件并完成构建验证

**Files:**
- Delete: `backend/src/controllers/market_controller.h`
- Delete: `backend/src/controllers/market_controller.c`
- Delete: `backend/src/services/market_service.h`
- Delete: `backend/src/services/market_service.c`

- [ ] **Step 1: 物理删除遗留文件**

```bash
git rm backend/src/controllers/market_controller.h \
       backend/src/controllers/market_controller.c \
       backend/src/services/market_service.h \
       backend/src/services/market_service.c
```

- [ ] **Step 2: 重新配置与构建后端**

Run: `cd backend && cmake -B build -G "Unix Makefiles" && cmake --build build --parallel`
Expected: 重新扫描源文件后编译成功，生成 `minefolio` 可执行文件，0 错误。

- [ ] **Step 3: 验证未残留对已删除文件的引用**

Run: `grep -rn "market_service.h" backend/src/ || true`
Run: `grep -rn "controllers/market_controller.h" backend/src/ || true`
Expected: 无任何匹配输出。

- [ ] **Step 4: 提交 Task 4 修改**

```bash
git commit -m "refactor(market): 🗑️ remove legacy market controller and service facades"
```

---

### Task 5: 完整质量门禁全量回归验证

**Files:**
- Verification only

- [ ] **Step 1: 运行核心门禁 `test_full.sh`**

Run: `cd backend && ./tests/test_full.sh`
Expected: PASS >= 153, FAIL = 0, exit 0.

- [ ] **Step 2: 运行 Market 专项测试 `test_market_sync.sh`**

Run: `cd backend && ./tests/test_market_sync.sh`
Expected: All 18 tests PASS, exit 0.

- [ ] **Step 3: 运行所有 26 个 CTest 单元测试套件**

Run: `cd backend/build && ctest --output-on-failure`
Expected: 26/26 Test suites passed, 100% tests passed.

- [ ] **Step 4: 运行 `test_link.sh` 基础集成测试**

Run: `cd backend && ./tests/test_link.sh`
Expected: All 38 cases (139 assertions) passed, exit 0.

- [ ] **Step 5: 运行前端编译类型检查门禁**

Run: `npm --prefix frontend run build`
Expected: `vue-tsc -b` 0 错误，Vite 构建成功生成 `dist/`。

- [ ] **Step 6: 文档与状态确认**

检查 `git status` 确认工作区干净，无未跟踪文件。
