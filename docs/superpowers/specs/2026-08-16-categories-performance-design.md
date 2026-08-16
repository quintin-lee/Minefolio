# 分类展示交互性能优化 — 设计文档

- 日期: 2026-08-16
- 范围: `frontend/src/views/Categories.vue`（单文件，纯前端）
- 状态: 已获用户批准

## 1. 背景与问题

分类管理页（`Categories.vue`）在默认 114 个分类下实测性能基线：

| 指标 | 基线值 |
|------|--------|
| 页面加载+渲染 | ~7.5s（其中固定等待 3s，实际渲染约 4.5s） |
| 总 DOM 节点数 | 5133 |
| 树节点数 | 114（`default-expand-all` 全量展开） |
| 树内按钮数 | 228（每个节点常显 编辑+删除） |
| API 数据量 | 16.7 KB（一次 `GET /categories` 全量树） |

根因：**网络不是瓶颈（16.7KB 仅几十 ms），瓶颈在 DOM 渲染** — 全量展开的树 × 每节点 2 个操作按钮产生 5000+ DOM 节点。

## 2. 方案选型

用户从三个候选中选定 **方案 A：纯前端渲染优化**。

- A（选定）：保持全量数据一次拉取 + store 缓存，只改渲染策略。零后端改动、单文件、风险最低。
- B（未选）：后端懒加载 `/children` 按需请求。payload 最小但每次展开有网络延迟，搜索需后端配合，双端改动。
- C（未选）：虚拟滚动。Element Plus 树不支持虚拟滚动，需引入新依赖或自研，复杂度最高。

选 A 的依据：114 节点 16.7KB 的全量拉取不是瓶颈；`/children` 接口已存在，未来分类规模上千时可叠加 B 的获取策略（store 换实现即可）。

## 3. 设计细节

### 3.1 数据层（`stores/category.ts` — 零改动）

维持 `GET /categories` 全量拉取 + Pinia 缓存。Assets/Transactions 页的级联选择器依赖 store 现有懒加载语义，不改 store 以避免连带风险。

### 3.2 树交互（`Categories.vue`）

| 改动点 | 现状 | 目标 |
|--------|------|------|
| 默认展开 | `default-expand-all`（114 节点全渲染） | 移除，仅渲染顶层节点（默认种子 22 个）；展开时子节点已在内存、零延迟 |
| 节点按钮 | 228 个按钮常显 | hover 才显示（CSS opacity 过渡；`:focus-within` 保证键盘可达） |
| 点击节点 | `onNodeClick` 空函数（死代码） | 选中节点 → 右侧表格对应行高亮 |
| 树高度 | 不固定 | 设固定 `height`，树面板内部滚动，与表格面板视觉对齐 |

### 3.3 搜索过滤

- 树面板 header 内加 `el-input`（`prefix-icon=Search`、`clearable`、`v-model="keyword"`）。
- 匹配范围：全量数据（`categories.value`），不受懒加载影响。
- 匹配规则：名称 `includes(keyword)`，大小写不敏感；**命中即保留该分支**（父链自动带上，避免节点悬空）。
- 过滤位置：`filteredCategories` computed 内追加 keyword 判断 — 树与表格共用同一份过滤结果。
- 空态：无匹配时树和表格均显示「暂无匹配分类」。

### 3.4 表格联动

- 树 `@node-click` 设置 `selectedId` → 表格行 `:class="{ 'row-selected': row.id === selectedId }"`（在现有 `premium-row` 基础上加高亮背景色）。
- 表格行点击反向设置 `selectedId`（双向联动）。
- 表格行数保持全量显示（过滤后更少），**不加分页**（YAGNI — 表格本身非瓶颈）。

### 3.5 其他清理

- 删除死代码：`onNodeClick` 空实现改为真实选中逻辑。
- `CHILD_ICONS` 硬编码表保留（现状逻辑，不在本次范围）。

## 4. 非目标（YAGNI）

- 后端接口改动（含新搜索接口）
- store 重构 / 懒加载语义变更
- 虚拟滚动 / 新依赖引入
- 表格分页
- 移动端专用布局（移动端走 `/m/*` 独立路由）

## 5. 验收标准

1. **性能**：Playwright 复测 — 首屏 DOM 节点 < 1500（基线 5133）；渲染时间 < 2s（基线 ~7.5s）。
2. **功能**：展开/折叠正常；搜索过滤含父链保留；树点选 → 表格高亮（双向）；增删改后 `store.invalidate()` 重载正常。
3. **回归**：`npm run build`（vue-tsc + vite）零错误；Assets/Transactions 页级联选择器不受影响。
4. **E2E**：现有 Playwright E2E 脚本全量通过。

## 6. 风险与回退

- 唯一改动文件为 `Categories.vue`，回退即 `git revert` 单个 commit。
- `default-expand-all` 移除后用户需手动展开 — 属预期交互变化，已在设计中与用户确认。
