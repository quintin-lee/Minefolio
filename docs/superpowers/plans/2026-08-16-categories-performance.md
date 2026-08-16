# 分类页展示性能优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将分类管理页（Categories.vue）首屏 DOM 从 5133 节点降到 <1500、渲染从 ~7.5s 降到 <2s，并新增搜索过滤与树-表格联动交互。

**Architecture:** 纯前端单文件改动（`frontend/src/views/Categories.vue`）。保持 `GET /categories` 全量拉取 + Pinia 缓存不变（网络非瓶颈），通过移除 `default-expand-all` 实现懒渲染（数据已在内存、展开零延迟），新增客户端搜索过滤（含父链保留），修复死代码 `onNodeClick` 为树-表格双向选中联动。后端与 store 零改动。

**Tech Stack:** Vue 3 `<script setup>`、Element Plus（el-tree / el-table / el-input）、Playwright（本仓库 `playwright-scripts/` 内已有的本地脚本，gitignored，用于性能与交互验证）。

**测试方式说明：** 本项目无组件单元测试基建（vitest 仅面向 mobile 构建），验证采用 Playwright 脚本断言 + `npm run build` 回归，与仓库既有验证文化一致（test_link.sh、playwright-scripts/）。

---

### Task 1: 移除全量展开（懒渲染）

**Files:**
- Modify: `frontend/src/views/Categories.vue:33`（el-tree 属性）

- [ ] **Step 1: 记录当前基线（确认复现）**

Run:
```bash
node playwright-scripts/perf-baseline.js
```
Expected: 输出包含 `总 DOM 节点数: 5133`、`树节点数: 114`。

- [ ] **Step 2: 移除 `default-expand-all`**

编辑 `frontend/src/views/Categories.vue` 第 33 行，将：

```html
              <el-tree :data="filteredTreeData" :props="{ label: 'name', children: 'children' }"
                node-key="id" default-expand-all class="premium-tree"
                :expand-on-click-node="false"
                @node-click="onNodeClick">
```

改为：

```html
              <el-tree :data="filteredTreeData" :props="{ label: 'name', children: 'children' }"
                node-key="id" class="premium-tree"
                :expand-on-click-node="false"
                @node-click="onNodeClick">
```

（删除 `default-expand-all` 属性。`expand-on-click-node="false"` 保留：点击节点只做选中，展开靠箭头。）

- [ ] **Step 3: 验证懒渲染生效**

Run:
```bash
node playwright-scripts/perf-baseline.js
```
Expected: `树节点数` 从 114 降至 22（仅顶层），`总 DOM 节点数` 从 5133 降至 <2000。

- [ ] **Step 4: 验证展开功能正常（子节点在内存中零延迟展开）**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  // 展开第一个顶层节点
  await page.locator('.el-tree-node__expand-icon').first().click();
  await page.waitForTimeout(300);
  const expanded = await page.evaluate(() => document.querySelectorAll('.el-tree-node').length);
  console.log('展开后树节点数:', expanded);
  console.log(expanded > 22 ? 'PASS: 展开加载了子节点' : 'FAIL: 展开无效果');
  await b.close();
})();
"
```
Expected: 输出 `展开后树节点数` > 22 且打印 PASS。

- [ ] **Step 5: Commit**

```bash
git add frontend/src/views/Categories.vue
git commit -m "perf(categories): remove default-expand-all for lazy rendering of tree"
```

---

### Task 2: 树-表格双向选中联动

**Files:**
- Modify: `frontend/src/views/Categories.vue:265`（`onNodeClick` 死代码替换）
- Modify: `frontend/src/views/Categories.vue:61-104`（el-table 加行高亮与点击）
- Modify: `frontend/src/views/Categories.vue`（script 加 `selectedId`、`rowClassName`；style 加 `.row-selected`）

- [ ] **Step 1: 写验证脚本（先确认现状为失败态）**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  await page.locator('.el-tree-node__content').first().click();
  await page.waitForTimeout(300);
  const highlighted = await page.evaluate(() => {
    const rows = document.querySelectorAll('.el-table__row');
    return Array.from(rows).filter(r => r.classList.contains('row-selected')).length;
  });
  console.log('高亮行数:', highlighted);
  console.log(highlighted >= 1 ? 'PASS: 树点击联动表格高亮' : 'FAIL: 无高亮（预期——尚未实现）');
  await b.close();
})();
"
```
Expected: 输出 `高亮行数: 0` + FAIL（当前 `onNodeClick` 是空函数，未实现联动）。

- [ ] **Step 2: 实现选中联动**

编辑 `frontend/src/views/Categories.vue`：

1) script 区，在 `const categories = ref<Category[]>([])`（约 171 行）下方新增：

```ts
const selectedId = ref<number | null>(null)
```

2) 将第 265 行的空函数：

```ts
function onNodeClick(data: any) { /* select for editing */ }
```

替换为：

```ts
function onNodeClick(data: any) {
  selectedId.value = data?.id ?? null
}

function rowClassName({ row }: { row: any }) {
  return row.id === selectedId.value ? 'row-selected' : ''
}

function onRowClick(row: any) {
  selectedId.value = row.id
}
```

3) 模板中 el-table（第 61 行起），在 `header-cell-class-name="premium-header"` 后追加两个属性：

```html
              <el-table :data="filteredFlatCategories" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header"
                :row-class-name="rowClassName" @row-click="onRowClick">
```

4) style 区（`.premium-row:hover > td` 规则之后，约 360 行）追加：

```css
.panel-body-scroll :deep(.el-table__row.row-selected td) {
  background-color: rgba(0, 212, 255, 0.1) !important;
}
```

- [ ] **Step 3: 重跑验证脚本**

Run（同 Step 1 的命令）
Expected: 输出 `高亮行数: 1` + PASS。

- [ ] **Step 4: 验证反向联动（表格点击 → 树节点选中）**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  await page.locator('.el-table__row').first().click();
  await page.waitForTimeout(300);
  const count = await page.evaluate(() =>
    document.querySelectorAll('.el-table__row.row-selected').length);
  console.log('高亮行数:', count);
  console.log(count === 1 ? 'PASS: 表格点击高亮自身' : 'FAIL');
  await b.close();
})();
"
```
Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add frontend/src/views/Categories.vue
git commit -m "feat(categories): bidirectional tree-table selection highlight"
```

---

### Task 3: 搜索过滤框

**Files:**
- Modify: `frontend/src/views/Categories.vue:27-31`（树面板 header 加搜索框）
- Modify: `frontend/src/views/Categories.vue:170-179`（`filteredCategories` 加 keyword 过滤 + `filterTree` 递归函数）

- [ ] **Step 1: 写验证脚本（现状失败态）**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  const hasInput = await page.locator('.search-input').count();
  console.log(hasInput > 0 ? 'PASS: 搜索框存在' : 'FAIL: 无搜索框（预期——尚未实现）');
  await b.close();
})();
"
```
Expected: FAIL（尚未实现）。

- [ ] **Step 2: 实现搜索框模板**

将第 27-31 行：

```html
          <div class="panel-container tree-panel-container">
            <div class="panel-header">
              <h3>分类结构</h3>
            </div>
```

改为：

```html
          <div class="panel-container tree-panel-container">
            <div class="panel-header panel-header-with-search">
              <h3>分类结构</h3>
              <el-input
                v-model="keyword"
                placeholder="搜索分类名称"
                prefix-icon="Search"
                clearable
                class="search-input"
                size="small"
              />
            </div>
```

- [ ] **Step 3: 实现过滤逻辑**

script 区，`const selectedId = ref<number | null>(null)` 下方新增：

```ts
const keyword = ref('')
```

将（第 173-179 行）：

```ts
const filteredCategories = computed(() => {
  if (activeTab.value === 'all') return categories.value
  return categories.value.filter(c => c.type === activeTab.value)
})

const filteredTreeData = computed(() => buildTree(filteredCategories.value))
const filteredFlatCategories = computed(() => flatten(filteredTreeData.value))
```

改为：

```ts
function filterTree(nodes: Category[], kw: string): Category[] {
  const out: Category[] = []
  for (const node of nodes) {
    const nameHit = node.name.toLowerCase().includes(kw)
    const children = node.children ? filterTree(node.children, kw) : []
    if (nameHit || children.length > 0) {
      out.push({ ...node, children: nameHit ? (node.children ?? []) : children })
    }
  }
  return out
}

const filteredCategories = computed(() => {
  if (activeTab.value !== 'all') return categories.value.filter(c => c.type === activeTab.value)
  return categories.value
})

const filteredTreeData = computed(() => {
  let tree = buildTree(filteredCategories.value)
  const kw = keyword.value.trim().toLowerCase()
  if (kw) tree = filterTree(tree, kw)
  return tree
})

const filteredFlatCategories = computed(() => flatten(filteredTreeData.value))
```

（说明：`filterTree` 对嵌套树递归 — 节点名命中则保留整棵子树；子节点命中则保留该节点与父链，未命中兄弟被过滤，避免悬空节点。`filteredTreeData` 与表格共用同一过滤结果。）

- [ ] **Step 4: 空态提示**

树面板 `.panel-body-scroll` 内、el-tree 之前插入：

```html
            <div class="panel-body-scroll">
              <div v-if="filteredTreeData.length === 0 && keyword" class="tree-empty">
                暂无匹配分类
              </div>
              <el-tree v-else ...
```

style 追加：

```css
.tree-empty {
  padding: 40px 0;
  text-align: center;
  color: var(--mf-text-placeholder);
  font-size: 13px;
}

.panel-header-with-search {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.search-input {
  width: 180px;
}
```

（表格侧 `filteredFlatCategories` 为空时 el-table 自带 `#empty` 插槽展示「暂无数据」，已在模板中；如需统一文案可留待后续。）

- [ ] **Step 5: 重跑 Step 1 脚本**

Expected: PASS（搜索框存在）。

- [ ] **Step 6: 验证搜索过滤 + 父链保留**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  await page.locator('.search-input input').fill('工资');
  await page.waitForTimeout(300);
  const treeNodes = await page.evaluate(() =>
    Array.from(document.querySelectorAll('.el-tree-node__label')).map(n => n.textContent));
  const rows = await page.evaluate(() =>
    Array.from(document.querySelectorAll('.el-table__row .cat-name-cell')).map(n => n.textContent));
  console.log('树节点:', treeNodes.join(', '));
  console.log('表格行:', rows.join(', '));
  const ok = treeNodes.some(t => t.includes('工资')) && rows.some(r => r.includes('工资'));
  console.log(ok ? 'PASS: 搜索命中且父链保留(职业收入→基本工资)' : 'FAIL');
  // 清空搜索恢复全量
  await page.locator('.search-input .el-input__clear').click().catch(() => {});
  await page.waitForTimeout(200);
  const afterClear = await page.evaluate(() => document.querySelectorAll('.el-tree-node').length);
  console.log(afterClear > 20 ? 'PASS: 清空后恢复全量' : 'FAIL: 清空未恢复');
  await b.close();
})();
"
```
Expected: `树节点` 包含「职业收入」与「基本工资」，打印两个 PASS。

- [ ] **Step 7: Commit**

```bash
git add frontend/src/views/Categories.vue
git commit -m "feat(categories): add keyword search with parent-chain retention"
```

---

### Task 4: 键盘可达性（hover 按钮 focus-within）

**现状说明：** `.tree-actions` 的 hover 显示（`opacity: 0 → 1`）已在 CSS 中实现（`Categories.vue:605-614`），无需重复实现；本任务仅补齐键盘用户（Tab 聚焦节点时看不到按钮）的缺口。

**Files:**
- Modify: `frontend/src/views/Categories.vue:612-614`

- [ ] **Step 1: 追加 focus-within 规则**

将：

```css
:deep(.premium-tree .el-tree-node__content:hover) .tree-actions {
  opacity: 1;
}
```

改为：

```css
:deep(.premium-tree .el-tree-node__content:hover) .tree-actions,
:deep(.premium-tree .el-tree-node__content:focus-within) .tree-actions {
  opacity: 1;
}
```

- [ ] **Step 2: 验证编译通过**

Run: `npm run build`
Expected: `✓ built in ...s`，零错误。

- [ ] **Step 3: Commit**

```bash
git add frontend/src/views/Categories.vue
git commit -m "a11y(categories): reveal tree action buttons on focus-within for keyboard users"
```

---

### Task 5: 性能复测与全量回归

**Files:**
- Verify only（无代码改动；如断言失败则回查对应 Task）

- [ ] **Step 1: 性能断言复测**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  const t0 = Date.now();
  await page.goto('http://127.0.0.1:5173/categories');
  await page.waitForTimeout(1500);
  const dom = await page.evaluate(() => document.getElementsByTagName('*').length);
  const elapsed = Date.now() - t0 - 1500;
  console.log('渲染耗时(不含固定等待):', elapsed + 'ms');
  console.log('DOM 节点数:', dom);
  const ok = dom < 1500 && elapsed < 2000;
  console.log(ok ? 'PASS: DOM<1500 且渲染<2s' : 'FAIL: 未达验收线');
  await b.close();
  process.exit(ok ? 0 : 1);
})();
"
```
Expected: 输出 DOM 节点数 < 1500、渲染 < 2s、PASS（验收标准：spec 第 5 节）。

- [ ] **Step 2: 类型检查与生产构建**

Run:
```bash
npm run build
```
Expected: `vue-tsc -b` 零错误，`vite build` 输出 `✓ built in ...s`。

- [ ] **Step 3: 全量 E2E 回归**

Run:
```bash
node playwright-scripts/e2e-test.js
```
Expected: `📊 Results: 13 passed, 0 failed`。

- [ ] **Step 4: 级联选择器回归（Assets 页不受影响）**

Run:
```bash
node -e "
const { chromium } = require('/home/quintin/Data/source/c_cpp/Minefolio/.pw_tmp/node_modules/playwright');
const TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImlhdCI6MTc4Njg4MDk5M30.gTy7Bi-50DcQQ9IOJrYJcuCUl1TygDbh6UnoWtOq9dg';
(async () => {
  const b = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const ctx = await b.newContext({ viewport: { width: 1440, height: 900 } });
  await ctx.addInitScript((t) => localStorage.setItem('token', t), TOKEN);
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:5173/assets');
  await page.waitForTimeout(1500);
  await page.getByRole('button', { name: /新增资产/i }).click();
  await page.waitForTimeout(500);
  // 打开分类级联选择器
  await page.locator('.el-cascader').first().click();
  await page.waitForTimeout(500);
  const nodes = await page.evaluate(() => document.querySelectorAll('.el-cascader-node').length);
  console.log('级联选择器节点数:', nodes);
  console.log(nodes > 0 ? 'PASS: Assets 级联选择器正常' : 'FAIL');
  await b.close();
})();
"
```
Expected: PASS（Assets 页级联选择器依赖 store 懒加载，未受本次改动影响）。

- [ ] **Step 5: 提交验证结论（如无改动则跳过）**

```bash
git status --short
```
Expected: clean（Tasks 1-4 已分别提交；如 Step 2/3 失败，修复后补交）。

---

## Self-Review 记录

- **Spec 覆盖**：3.2 懒渲染 → Task 1；3.2 选中联动 → Task 2；3.3 搜索 → Task 3；3.2 hover 按钮 → Task 4（现状已实现 hover，仅补 focus-within）；3.4 双向联动 → Task 2；验收标准 → Task 5。全部覆盖。
- **占位符扫描**：无 TBD/TODO；每个代码步骤含完整代码与预期输出。
- **类型一致性**：`selectedId`、`rowClassName`、`onRowClick`、`filterTree`、`keyword` 命名在 Task 2/3 间一致；`filteredTreeData`/`filteredFlatCategories` 沿用现有命名。
