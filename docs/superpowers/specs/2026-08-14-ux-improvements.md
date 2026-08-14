# 全项目体验改进 Spec

> **日期**: 2026-08-14
> **状态**: 设计已确认，待实现（用户已批准 P0+P1+P2 全量范围）
> **来源**: 基于全页面审查（Dashboard / Assets / Holdings / Transactions / DailyExpenses / Reports / Settings / Login / Setup）
> **优先级**: P0 → P1 → P2 分阶段实现
> **决策记录**: 用户确认分步表单按类型分组两步、导出用 blob 直下、P2 全做（SummaryCard/标题统一/formatCurrency 统一）、按优先级分批 commit

---

## 背景

Minefolio 已完成核心功能闭环：资产增删改、投资类自动推导市值、持仓盈亏报表、交易 CRUD、日常收支、分类管理、登录/初始化。技术债已清零（PASS=103 FAIL=0）。下一阶段聚焦**用户体验打磨**，覆盖所有页面的交互一致性与可用性。

---

## P0 — 阻塞核心任务流（立即实现）

### P0-1 · 交易表单手续费字段重复渲染

**问题**
`Transactions.vue` 中存在两处 `<el-form-item label="手续费">` 块（lines 288–297 和 299–312），在买入/卖出类型下渲染两次相同输入框，用户会困惑或误填两次。

**目标**
删除第二个重复块，只保留一份。

**验收标准**
- 打开新增交易弹窗，选「买入」→ 表单中「手续费」字段只出现一次
- 预算金额自动计算（数量 × 单价 − 手续费）不受影响
- `bash backend/tests/test_link.sh` 仍 PASS=103 FAIL=0

**文件**
- `frontend/src/views/Transactions.vue`

---

### P0-2 · 新增交易默认选中高频类型

**问题**
当前 `form.transaction_type` 默认值为 `'deposit'`（存入），但用户最高频操作是「买入」（投资资产）和「转出」（转钱到外部账户）。

**目标**
将默认类型改为 `'buy'`；同时按当前月份的交易类型频次动态调整（可选）。

**验收标准**
- 打开新增交易弹窗，交易类型下拉默认高亮「买入」
- 切换到「取出」等低频类型后再次点「新增交易」，默认仍为「买入」
- 表单其他字段（资产账户、分类、日期）保持原默认值

**文件**
- `frontend/src/views/Transactions.vue`（`form.transaction_type` 初始值从 `'deposit'` 改为 `'buy'`）

---

### P0-3 · 持仓页浮动盈亏卡片按盈亏着色

**问题**
浮盈/浮亏数字颜色（income-text/expense-text）正确，但汇总卡片背景为中性色，用户快速扫视时盈亏方向不明显。

**目标**
为「总浮动盈亏」卡片根据盈亏正负动态添加 `profit-card` / `loss-card` class，背景分别为淡绿/淡红。

**验收标准**
- 总浮动盈亏 > 0：卡片背景 `rgba(52, 211, 153, 0.08)`，边框 `rgba(52, 211, 153, 0.3)`
- 总浮动盈亏 < 0：卡片背景 `rgba(239, 68, 68, 0.08)`，边框 `rgba(239, 68, 68, 0.3)`
- 总浮动盈亏 = 0：使用默认中性背景
- 与 Assets.vue 的 `.highlight-card` 风格协调但不冲突（不替换 highlight-card，而是叠加）
- 新建空数据库时显示中性背景

**文件**
- `frontend/src/views/Holdings.vue`

---

## P1 — 体验显著提升（短期实现）

### P1-1 · Reports 各 Tab 空状态提示

**问题**
用户在新建账户后打开「报表中心」，看到两个空白的图表区域，不知道是因为还没有数据还是功能异常。

**目标**
每个 Tab 内容区（收支分析、资产分析）在无数据时显示 `<el-empty>` 引导卡片，文案指向「先去录入一笔交易」。

**验收标准**
- 空数据时「收支分析」Tab 显示 `暂无收支数据，完成第一笔交易后这里会出现图表`
- 空数据时「资产分析」Tab 显示 `暂无资产数据，先添加资产账户`
- 有数据时正常渲染图表，el-empty 不出现
- 不改变现有 API 调用逻辑

**文件**
- `frontend/src/views/Reports.vue`

---

### P1-2 · 编辑投资资产时份额字段只读

**问题**
当前编辑投资类资产时，份额/净值/成本三个字段均可修改。用户不理解「为什么改了份额，市值就变了」，产生困惑；且份额修改语义上等同于重新建仓，不应在编辑场景发生。

**目标**
编辑模式下「持有份额」字段设为只读（disabled），仅「单位净值」可编辑；成本字段根据净值的变更自动重算（保持与后端对齐：`cost_basis` 允许手动覆盖，但默认 = qty × net_value）。

**验收标准**
- 新增模式：三个字段均可编辑（与原行为一致）
- 编辑模式：份额字段 disabled，灰色显示；净值字段可编辑；成本字段可编辑（允许用户手动调整成本basis）
- 编辑提交后，后端 PUT 行为不变（已有 I1b 测试覆盖 net_value 更新）
- 表单底部 hint 文字更新：编辑时显示「净值更新后将重新计算市值」

**文件**
- `frontend/src/views/Assets.vue`

---

### P1-3 · 交易表单改为分步引导

**问题**
新增交易弹窗一次性展开 10+ 个字段，新用户面对空白表单不知从何填起；老用户也需频繁滚动。

**目标**
采用两步表单：
- **步骤 1（基础信息）**：交易类型（必选）→ 根据类型显示对应字段组（买入/卖出显示单价×数量，存入/转出只显示金额）
- **步骤 2（完整信息）**：资产账户、资金账户、分类、日期、备注

步骤之间用 `ElSteps` 组件展示进度，步骤 1 完成后点击「下一步」进入步骤 2。

**验收标准**
- 打开新增弹窗，默认显示步骤 1，仅展示交易类型 + 根据类型动态渲染的核心字段
- 步骤 1 验证通过后才能进入步骤 2
- 步骤 2 包含其余所有字段（资产账户、资金账户、分类、日期、备注）
- 编辑模式保持单步（无需分步，减少编辑场景的摩擦）
- 手机端（≥768px 宽度）仍为单步，不触发分步逻辑（避免窄屏体验差）

**文件**
- `frontend/src/views/Transactions.vue`

---

### P1-4 · Settings 扩展 + 改密后不强制登出

**问题**
Settings 页面只有一个改密码表单，标题却是「系统设置」，用户期待更多内容。改密成功后强制跳转到登录页，体验割裂。

**目标**
1. 页面扩展为三个区块：「账户信息」（只读）/「修改密码」/「数据导出」
2. 改密成功后留在当前页，仅刷新用户名显示
3. 「数据导出」区块放一个按钮，链接到 `/transactions?export=true` 或直接调用 exportCsv API

**验收标准**
- 账户信息区块显示用户名、注册时间（已有），不可编辑
- 修改密码表单保持原有验证逻辑，提交成功后显示 `密码修改成功` toast，留在本页
- 不触发 router.push('/login')，auth token 保持有效
- 「数据导出」按钮点击后触发 `/api/export/transactions` blob 下载

**文件**
- `frontend/src/views/Settings.vue`
- `frontend/src/api/transactions.ts`（复用 exportCsv 逻辑）

---

## P2 — 一致性打磨（中期实现）

### P2-1 · 统一页面标题命名规范

**问题**
当前标题混用「资产管理」「持仓管理」「交易记录」「日常收支」「报表中心」「系统设置」，有的带「管理」有的不带，风格不统一。

**目标**
统一去掉冗余后缀，全部用简洁名词：
- 仪表盘（不变）
- 资产（原「资产管理」）
- 持仓（不变）
- 交易（原「交易记录」）
- 收支（原「日常收支」）
- 分类（不变）
- 报表（原「报表中心」）
- 日志（不变）
- 设置（原「系统设置」）

同步更新 `Layout.vue` 的 `pageTitle` map 和 `zh-CN.ts` 的 nav 翻译键。

**验收标准**
- 9 个菜单项显示名称与标题一致
- `grep '资产管理\|交易记录\|日常收支\|报表中心\|系统设置' frontend/src/` 无命中
- 浏览器访问各路由，顶部 h2 标题符合新命名

**文件**
- `frontend/src/views/Layout.vue`
- `frontend/src/locales/zh-CN.ts`

---

### P2-2 · 统一摘要卡片组件

**问题**
Dashboard 用 `<el-card shadow="hover">`，Assets/Holdings/DailyExpenses 用自定义 `.summary-card` div，视觉不完全一致，维护成本高。

**目标**
抽取 `<SummaryCard :label string :value string :type 'income'|'expense'|'neutral'|'highlight'>` 组件，所有页面的 4 张汇总卡统一使用。

**验收标准**
- Dashboard 4 张卡（总资产/总负债/净资产/本月结余）迁移到新组件
- Assets.vue 3 张卡迁移
- Holdings.vue 3 张卡迁移
- DailyExpenses.vue 3 张卡迁移
- 迁移后视觉无变化（像素级一致）
- 新组件支持 `profit-card`/`loss-card` 动态背景（服务于 P0-3）

**文件**
- `frontend/src/components/SummaryCard.vue`（新建）
- `frontend/src/views/Dashboard.vue`
- `frontend/src/views/Assets.vue`
- `frontend/src/views/Holdings.vue`
- `frontend/src/views/DailyExpenses.vue`

---

### P2-3 · 统一货币格式化

**问题**
部分页面用 `import { formatCurrency } from '@/utils/format'`，部分页面内联 `new Intl.NumberFormat(...)`。

**目标**
所有页面统一使用 `@/utils/format` 中的 `formatCurrency`；若该函数不存在则创建。

**验收标准**
- `grep -r "Intl.NumberFormat" frontend/src/views/` 无命中（允许 components 目录内个别图表组件使用）
- 所有页面的金额显示格式一致（zh-CN locale，CNY 符号）

**文件**
- `frontend/src/utils/format.ts`（确认或创建）
- 各 `.vue` 文件导入处替换

---

## P3 — 长期规划（后续迭代，本次不实现）

1. **移动端响应式**：侧边栏收为汉堡菜单（el-drawer），图表缩为 200px，表格只保留名称+市值列
2. **图表-表格双向联动**：饼图点击扇区 → 表格过滤对应资产类型；柱图点击柱子 → 表格过滤对应资产
3. **多语言支持**：补充 en-US locale，至少覆盖 nav + 表单 label + 错误提示
4. **资产列表排序**：支持按名称/市值/更新时间排序，默认按创建时间倒序
5. **Dashboard 最近收支「查看全部」链接**：点击跳转至 Transactions 页并预填过滤条件
6. **分类管理拖拽排序**：支持 drag-and-drop 调整分类顺序

---

## 验证计划

每个 P0/P1 改完后执行：

```bash
cmake --build backend/build --parallel && \
npm --prefix frontend run build && \
bash backend/tests/test_link.sh
```

期望结果：后端 100% Built，前端 0 errors，**PASS=103 FAIL=0**。

P2 改动仅需前端构建通过（不改变后端行为）。

---

## 不在本次范围内

- 后端新接口开发（P3 功能）
- 数据库 schema 变更
- 认证流程修改
- 新的图表类型（新增 ECharts 组件）
- 国际化（i18n）框架扩展
