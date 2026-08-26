# UX Improvements (P0 + P1) Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish all Minefolio pages for a consistent, user-friendly experience — fixing blocking UX bugs (P0) and adding meaningful quality-of-life improvements (P1).

**Architecture:** All changes are frontend-only Vue/TypeScript; no backend API or schema changes. The plan is structured as sequential batches (P0 → P1), each with its own build+test verification gate. P2 (component extraction + naming unification) is covered in a separate plan due to broader cross-file impact.

**Tech Stack:** Vue 3 + TypeScript + Element Plus + ECharts + Pinia. Build: `npm --prefix frontend run build` (= vue-tsc -b && vite build). Backend integration tests: `bash backend/tests/test_link.sh` (expect PASS=103 FAIL=0 throughout — P0/P1 don't touch backend).

---

## File Map

| File | Responsibility |
|------|---------------|
| `frontend/src/views/Transactions.vue` | P0-1 (remove dup fee), P0-2 (default type), P1-2 (edit readonly), P1-3 (step form) |
| `frontend/src/views/Holdings.vue` | P0-3 (dynamic float-card class) |
| `frontend/src/views/Reports.vue` | P1-1 (tab empty states) |
| `frontend/src/views/Assets.vue` | P1-2 (readonly quantity in edit mode) |
| `frontend/src/views/Settings.vue` | P1-4 (expand + export CSV) |

All 5 files are ≤950 lines each; changes are surgical (template + small script edits). No new dependencies.

---

## Batch 1: P0 — 阻塞核心任务流 (3 items, ~30 min)

### Task 1-1: P0-1 · 删除交易表单重复手续费字段

**Files:**
- Modify: `frontend/src/views/Transactions.vue` lines 299–312

- [ ] **Step 1: 确认重复字段位置**
  ```bash
  grep -n "手续费" frontend/src/views/Transactions.vue
  ```
  预期：至少 2 行（288、301 附近），分别对应两个 el-form-item 块。

- [ ] **Step 2: 删除第二个块（lines 299–312）**
  删除以下代码块（含前后空行）：
  ```html
  <el-row v-if="isTradingType(form.transaction_type)" :gutter="16" class="trading-fields">
    <el-col :span="12">
      <el-form-item label="手续费">
        <el-input-number
          v-model="form.fee"
          :precision="2"
          :min="0"
          style="width: 100%"
          :controls="false"
          placeholder="0.00"
        />
      </el-form-item>
    </el-col>
  </el-row>
  ```
  保留第一个块（lines 286–297）不动。

- [ ] **Step 3: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors（可能有余量 warning 如 chunk-size）。

- [ ] **Step 4: 集成测试**
  ```bash
  bash backend/tests/test_link.sh
  ```
  预期：**PASS=103 FAIL=0**（不触碰后端，结果不变）。

- [ ] **Step 5: 提交**
  ```bash
  git add frontend/src/views/Transactions.vue
  git commit -m "fix: remove duplicate fee field in transaction form"
  ```

---

### Task 1-2: P0-2 · 新增交易默认选中「买入」

**Files:**
- Modify: `frontend/src/views/Transactions.vue` line 460, 634

- [ ] **Step 1: 修改 form 初始值**
  找到 `transaction_type: 'deposit'` 改为 `'buy'`（约 line 460）：
  ```ts
  transaction_type: 'buy' as Transaction['transaction_type'],
  ```

- [ ] **Step 2: 修改 openDialog 新建分支默认值**
  找到 openDialog 的 else 分支中 `transaction_type: 'deposit'` 改为 `'buy'`（约 line 634）：
  ```ts
  transaction_type: 'buy',
  ```

- [ ] **Step 3: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors。

- [ ] **Step 4: 提交**
  ```bash
  git add frontend/src/views/Transactions.vue
  git commit -m "fix: default transaction type to buy instead of deposit"
  ```

---

### Task 1-3: P0-3 · 持仓页浮动盈亏卡片按盈亏着色

**Files:**
- Modify: `frontend/src/views/Holdings.vue`

- [ ] **Step 1: 读取 Holdings.vue 当前汇总卡模板**
  ```bash
  grep -n "总浮动盈亏\|summary-card\|card-wrapper" frontend/src/views/Holdings.vue
  ```

- [ ] **Step 2: 添加动态 class**
  找到「总浮动盈亏」的 card wrapper div（类似 `<div class="summary-card">`），追加动态 class：
  ```html
  <div class="summary-card" :class="floatCardClass">
  ```
  在 script 中新增 computed：
  ```ts
  const floatCardClass = computed(() => {
    const pnl = report.value?.summary.total_floating_pnl ?? 0
    if (pnl > 0) return 'profit-card'
    if (pnl < 0) return 'loss-card'
    return ''
  })
  ```

- [ ] **Step 3: 添加 CSS 样式**
  在 `<style scoped>` 中添加：
  ```css
  .profit-card {
    background: rgba(52, 211, 153, 0.08);
    border-color: rgba(52, 211, 153, 0.3);
  }
  .loss-card {
    background: rgba(239, 68, 68, 0.08);
    border-color: rgba(239, 68, 68, 0.3);
  }
  ```

- [ ] **Step 4: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors。

- [ ] **Step 5: 集成测试**
  ```bash
  bash backend/tests/test_link.sh
  ```
  预期：**PASS=103 FAIL=0**。

- [ ] **Step 6: 提交**
  ```bash
  git add frontend/src/views/Holdings.vue
  git commit -m "feat: colorize floating PnL card by profit/loss direction"
  ```

---

## Batch 2: P1 — 体验显著提升 (4 items, ~90 min)

### Task 2-1: P1-1 · Reports 各 Tab 空状态提示

**Files:**
- Modify: `frontend/src/views/Reports.vue`

- [ ] **Step 1: 阅读 Reports.vue 结构**
  ```bash
  grep -n "el-tab-pane\|el-empty\|暂无" frontend/src/views/Reports.vue | head -20
  ```
  找到收支分析和资产分析两个 tab 的内容区位置。

- [ ] **Step 2: 为收支分析 Tab 添加空状态**
  在收支分析的内容区域（通常是图表容器）外层添加条件渲染：
  ```html
  <div v-if="!hasIncomeExpenseData" class="empty-state">
    <el-empty description="暂无收支数据，完成第一笔交易后这里会出现图表" />
  </div>
  <div v-else>
    <!-- 原有图表内容 -->
  </div>
  ```
  新增 computed：
  ```ts
  const hasIncomeExpenseData = computed(() => {
    return monthly.value && (monthly.value.total_income > 0 || monthly.value.total_expense > 0)
  })
  ```

- [ ] **Step 3: 为资产分析 Tab 添加空状态**
  同样模式，判断 `assetBreakdown` 是否有数据：
  ```html
  <div v-if="!hasAssetBreakdownData" class="empty-state">
    <el-empty description="暂无资产数据，先添加资产账户" />
  </div>
  ```
  ```ts
  const hasAssetBreakdownData = computed(() => {
    return !!assetBreakdown.value?.total_assets && assetBreakdown.value.total_assets > 0
  })
  ```

- [ ] **Step 4: 添加空状态样式**
  ```css
  .empty-state {
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 200px;
  }
  ```

- [ ] **Step 5: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors。

- [ ] **Step 6: 提交**
  ```bash
  git add frontend/src/views/Reports.vue
  git commit -m "feat: add empty state hints for Reports tabs"
  ```

---

### Task 2-2: P1-2 · 编辑投资资产时份额字段只读

**Files:**
- Modify: `frontend/src/views/Assets.vue`

- [ ] **Step 1: 定位持有份额输入框**
  ```bash
  grep -n "持有份额\|form.quantity\|isInvestment" frontend/src/views/Assets.vue
  ```

- [ ] **Step 2: 为持有份额添加 :disabled**
  找到持有份额的 el-input-number，添加 `:disabled="editingId !== null"`：
  ```html
  <el-input-number v-model="form.quantity" :precision="4" :min="0" style="width: 100%" :controls="false" :disabled="editingId !== null" />
  ```

- [ ] **Step 3: 更新编辑模式的 hint 文字**
  找到 `.investment-hint` 元素，根据编辑/新建动态切换文案：
  ```html
  <div class="investment-hint">{{ editingId ? '净值更新后将重新计算市值' : '市值将按 份额 × 净值 自动计算；成本留空则等同市值' }}</div>
  ```

- [ ] **Step 4: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors。

- [ ] **Step 5: 集成测试**
  ```bash
  bash backend/tests/test_link.sh
  ```
  预期：**PASS=103 FAIL=0**（不触碰后端 PUT 行为，I1b 测试仍通过）。

- [ ] **Step 6: 提交**
  ```bash
  git add frontend/src/views/Assets.vue
  git commit -m "fix: disable quantity field in edit mode for investment assets"
  ```

---

### Task 2-3: P1-3 · 交易表单改为分步引导

**Files:**
- Modify: `frontend/src/views/Transactions.vue`

- [ ] **Step 1: 分析当前表单结构**
  确定步骤划分：
  - Step 1：交易类型 + 核心字段（买入/卖出：单价×数量；存入/转出/取出/转入：金额）
  - Step 2：资产账户、资金账户、分类、日期、备注

- [ ] **Step 2: 引入 ElSteps 组件**
  在 template 的 dialog 内顶部添加：
  ```html
  <el-steps :active="step" finish-status="success" align-center>
    <el-step title="基础信息" description="选择类型并填写核心字段" />
    <el-step title="完整信息" description="选择账户、分类和日期" />
  </el-steps>
  ```

- [ ] **Step 3: 添加 step 状态**
  ```ts
  const step = ref(0)
  ```

- [ ] **Step 4: 拆分表单内容**
  用 `v-if="step === 0"` / `v-else` 包裹两个步骤的表单内容。Step 1 包含：
  - 交易类型（必填）
  - 根据类型动态显示：
    - buy/sell：单价、数量、手续费
    - 其他：金额
  Step 2 包含：
  - 资产账户、资金账户、分类、日期、备注

- [ ] **Step 5: 添加步骤导航按钮**
  在 footer 替换为：
  ```html
  <el-button @click="dialogVisible = false">取消</el-button>
  <el-button v-if="step > 0" @click="step--">上一步</el-button>
  <el-button v-else type="primary" @click="dialogVisible = false">取消</el-button>
  <el-button v-if="step < 1" type="primary" @click="nextStep">下一步</el-button>
  <el-button v-else type="primary" :loading="saving" @click="handleSubmit">保存</el-button>
  ```

- [ ] **Step 6: 实现 nextStep 验证**
  ```ts
  function nextStep() {
    if (step.value === 0) {
      // 验证 Step 1 必填项：transaction_type + (amount OR quantity)
      if (!form.transaction_type) {
        ElMessage.warning('请选择交易类型')
        return
      }
      if (isTradingType(form.transaction_type)) {
        if (!form.quantity || form.quantity <= 0 || !form.price_per_unit || form.price_per_unit <= 0) {
          ElMessage.warning('请填写完整的单价和数量')
          return
        }
      } else {
        if (!form.amount || form.amount <= 0) {
          ElMessage.warning('请填写交易金额')
          return
        }
      }
      step.value++
    }
  }
  ```

- [ ] **Step 7: 编辑模式跳过分步**
  在 openDialog 中，如果是 edit 模式，直接设置 `step.value = 1`：
  ```ts
  if (txn?.id) {
    step.value = 1
  }
  ```

- [ ] **Step 8: 响应式 —— 窄屏禁用分步**
  使用 computed 判断宽度（或直接用 CSS media query 隐藏 ElSteps）：
  ```ts
  const isNarrow = window.innerWidth < 768
  // 或在 template 中 :class="{ 'step-form': !isEditing }"
  ```
  更简单的做法：在 template 中 `v-if="!editingId"` 显示 ElSteps，编辑模式始终 step=1。

- [ ] **Step 9: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors（注意 TS 类型安全，避免 any）。

- [ ] **Step 10: 集成测试**
  ```bash
  bash backend/tests/test_link.sh
  ```
  预期：**PASS=103 FAIL=0**。

- [ ] **Step 11: 手动 smoke test（浏览器）**
  - 打开 /transactions → 点「新增交易」→ 应显示步骤 1（仅类型+核心字段）
  - 选「买入」→ 出现单价×数量
  - 点「下一步」→ 验证通过，进入步骤 2
  - 点「返回」→ 回到步骤 1
  - 点「保存」→ 交易创建成功
  - 编辑现有交易 → 直接显示完整表单（不分步）

- [ ] **Step 12: 提交**
  ```bash
  git add frontend/src/views/Transactions.vue
  git commit -m "feat: add step-by-step wizard to new transaction form"
  ```

---

### Task 2-4: P1-4 · Settings 扩展 + 改密后不强制登出

**Files:**
- Modify: `frontend/src/views/Settings.vue`
- Read: `frontend/src/api/transactions.ts` (已有 exportCsv)

- [ ] **Step 1: 阅读当前 Settings.vue**
  ```bash
  cat frontend/src/views/Settings.vue
  ```
  了解现有结构（只有改密码表单）。

- [ ] **Step 2: 添加账户信息区块**
  在改密码表单前添加只读信息：
  ```html
  <el-card shadow="never" class="settings-section">
    <template #header><span>账户信息</span></template>
    <el-descriptions :column="1" border>
      <el-descriptions-item label="用户名">{{ authStore.user?.username || '—' }}</el-descriptions-item>
      <el-descriptions-item label="注册时间">{{ formatDate(authStore.user?.created_at) }}</el-descriptions-item>
    </el-descriptions>
  </el-card>
  ```
  确保已 import `useAuthStore` 和 `formatDate`（从 `@/utils/format`）。

- [ ] **Step 3: 修改改密成功行为**
  找到改密成功的回调，移除 `router.push('/login')`，改为：
  ```ts
  ElMessage.success('密码修改成功')
  // 刷新用户信息
  await authStore.fetchUser()
  ```
  （如果 authStore 没有 fetchUser，改用 `authApi.me()` 重新获取）。

- [ ] **Step 4: 添加数据导出区块**
  ```html
  <el-card shadow="never" class="settings-section">
    <template #header><span>数据导出</span></template>
    <el-button type="primary" @click="handleExport">导出交易记录 CSV</el-button>
    <p class="export-hint">导出当前账号下所有交易记录，包含日期、类型、金额、分类等字段。</p>
  </el-card>
  ```

- [ ] **Step 5: 实现 handleExport**
  ```ts
  function handleExport() {
    import('@/utils/http').then(({ default: http }) => {
      http.get('/export/transactions', { responseType: 'blob' }).then((blob: Blob) => {
        const url = URL.createObjectURL(blob)
        const a = document.createElement('a')
        a.href = url
        a.download = `minefolio_transactions_${new Date().toISOString().slice(0,10)}.csv`
        document.body.appendChild(a)
        a.click()
        document.body.removeChild(a)
        URL.revokeObjectURL(url)
      }).catch(() => ElMessage.error('导出失败'))
    })
  }
  ```
  或直接复用 Transactions.vue 的 `exportCsv` 函数逻辑（DRY：提取到共享工具函数）。

- [ ] **Step 6: 前端构建验证**
  ```bash
  npm --prefix frontend run build
  ```
  预期：0 errors。

- [ ] **Step 7: 手动 smoke test（浏览器）**
  - 打开 /settings → 应看到三个区块：账户信息 / 修改密码 / 数据导出
  - 修改密码 → 成功后 toast 提示，页面不跳转，用户名显示刷新
  - 点「导出交易记录 CSV」→ 触发文件下载

- [ ] **Step 8: 提交**
  ```bash
  git add frontend/src/views/Settings.vue
  git commit -m "feat: expand Settings page with account info and CSV export"
  ```

---

## Verification Gate (run after each batch)

```bash
cmake --build backend/build --parallel && \
npm --prefix frontend run build && \
bash backend/tests/test_link.sh
```
期望：后端 100% Built，前端 0 errors，**PASS=103 FAIL=0**。

---

## Notes for Agent Worker

- **P0-1**: Simple deletion — make sure you delete the RIGHT block (the second one, not the first). Check line numbers after reading the file.
- **P0-2**: Two occurrences of `'deposit'` to change — check both form init and openDialog else branch.
- **P1-2**: `editingId` is already a ref in Assets.vue — use it directly in the template's `:disabled` binding.
- **P1-3**: The step form is the most complex change. Key insight: edit mode ALWAYS shows step 1 (full form), new mode starts at step 0. Validation on step transition must check type-appropriate required fields. Don't over-engineer the responsive behavior — just hide ElSteps visually on narrow screens via CSS if needed; the spec says "≥768px 宽度仍为单步" which means the step UI itself should be hidden on narrow screens, but form content is always full.
- **P1-4**: Check if `authStore` has a `refresh` or `fetchUser` method — if not, call `authApi.me()` directly. The export CSV logic can be extracted to a shared helper if reused elsewhere (Transactions.vue already has `exportCsv`).
- **No backend changes**: All tasks are frontend-only. Do NOT touch `backend/src/` or `backend/tests/`.
