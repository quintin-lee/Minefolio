# UI 现代化重构 — Phase 1: 视觉设计系统与 Design Tokens 重构

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建现代化、高质感、低眩光的金融科技 Design Tokens 体系，支持深色/浅色/自适应主题切换，规范金融级等宽数字排版与 Element Plus 全局组件样式。

**Architecture:** 
- 样式层：重构 `src/styles/index.css`，定义 `--mf-*` 基础设计变量与 `[data-theme="light"]` / `[data-theme="dark"]` 主题映射，全面重载 Element Plus 变量。
- 状态层：新增 `src/stores/theme.ts` Pinia Store，管理主题模式并自动监听系统首选项。
- 组件层：开发 `ThemeToggle.vue` 切换器并在 `Layout.vue` 顶栏挂载。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, CSS Custom Properties.

---

### Task 1: 创建主题状态管理 Store (Pinia)

**Files:**
- Create: `frontend/src/stores/theme.ts`

- [ ] **Step 1: 编写 theme store 代码**

---

### Task 2: 全面升级 CSS Design Tokens 与暗浅双色主题体系

**Files:**
- Modify: `frontend/src/styles/index.css`

- [ ] **Step 1: 重构 Design Tokens，包括 FinTech 配色、玻璃拟态、等宽数字与 Element Plus 全局重载**
- [ ] **Step 2: 编写辅助样式（等宽数字类、胶囊徽章、骨架屏微光）**

---

### Task 3: 创建顶栏主题切换器组件 (ThemeToggle)

**Files:**
- Create: `frontend/src/components/ThemeToggle.vue`
- Modify: `frontend/src/views/Layout.vue`

- [ ] **Step 1: 实现 ThemeToggle.vue**
- [ ] **Step 2: 在 Layout.vue 的 Header 右侧嵌入 ThemeToggle**

---

### Task 4: 构建与视觉一致性回归验证

- [ ] **Step 1: 运行前端编译校验**
- [ ] **Step 2: 运行全量集成测试**
