# UI 现代化重构 — Phase 2: 布局架构与导航体系重塑

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构侧边栏多层级分组导航、实现顶栏一键记账入口 (Quick Record Modal) 与全局搜索交互，提升全站信息架构清晰度与快捷操作效率。

**Architecture:**
- 侧边栏：在 `Layout.vue` 中将 12 个平铺菜单重构为 4 个业务模块分组（资产全景、收支管理、AI 智能空间、系统管理），支持展开与折叠两种形态。
- 全局顶栏：在 `Layout.vue` 顶栏增加「+ 记账」快捷主按钮与快捷键 `N` 监听。
- 快捷记账组件：开发 `src/components/QuickRecordDialog.vue`，支持日常收支（支出/收入）与资产交易快速录入，联动更新。

---

### Task 1: 侧边栏业务模块分组重构

**Files:**
- Modify: `frontend/src/views/Layout.vue`

- [ ] **Step 1: 重构 Layout.vue el-menu 结构为 4 组模块，添加分类标题**
- [ ] **Step 2: 编写分组标题样式与折叠适配**

---

### Task 2: 开发全局快捷记账弹窗组件 (QuickRecordDialog)

**Files:**
- Create: `frontend/src/components/QuickRecordDialog.vue`

- [ ] **Step 1: 开发 QuickRecordDialog.vue (支持快速记支出/收入/转账)**
- [ ] **Step 2: 在 Layout.vue 中挂载并绑定快捷键 N 与全局按钮**

---

### Task 3: 构建与回归测试验证

- [ ] **Step 1: 运行前端构建校验**
- [ ] **Step 2: 运行后端集成测试**
