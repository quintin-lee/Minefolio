# 前端左侧菜单栏收起功能设计规范 (Sidebar Collapse Design)

## 1. 概述与目标

为提升宽屏与小分辨率屏幕下的内容可视区域与操作体验，在桌面端为 Minefolio 的左侧侧边栏引入**折叠/收起 (Collapse / Expand)** 功能。

---

## 2. 核心交互与行为定义

1. **触发方式**：
   - 在顶部 Header 左侧（页面标题前）放置桌面端折叠切换按钮（图标采用 `ph:sidebar-simple` 或折叠图标）。
   - 桌面端（宽度 >= 768px）点击触发 `isCollapsed` 切换；
   - 移动端（宽度 < 768px）保持原有的移动端抽屉汉堡菜单逻辑，两者互不干扰。
   - 悬停在折叠按钮上提供 ElTooltip 提示（“收起菜单” / “展开菜单”）。

2. **收起状态形态**：
   - 侧边栏宽度在 `260px`（展开）与 `64px`（收起）之间平滑过渡（`transition: width 0.25s cubic-bezier(0.4, 0, 0.2, 1)`）。
   - Logo 区域：收起时保持 Logo 图标居中，文字 `Minefolio` 优雅淡出隐藏；
   - 菜单部分：启用 `el-menu` 的 `:collapse="isCollapsed"` 模式，收起时仅展示对应菜单项的图标，鼠标悬停时弹出 Element Plus 原生子菜单浮层/Tooltip；
   - 激活状态（Active Indicator）：收起状态下高亮条调整适配 64px 宽度，避免布局错位。

3. **状态持久化**：
   - 状态键名：`localStorage.getItem('sidebar_collapsed')`；
   - 用户切换折叠状态时实时同步写入 `localStorage`；
   - 页面刷新或路由跳转时保持用户设定的折叠偏好。

---

## 3. 架构与改动范围

- **受影响文件**：`frontend/src/views/Layout.vue`
- **依赖库**：无新增依赖，复用 Element Plus `el-menu` 内置 collapse 能力与 `@iconify/vue` 图标。
- **暗黑主题适配**：针对 `el-menu--collapse` 浮层菜单补充深色背景与描边样式，与整体 Cyberpunk/Slate 风格高度统一。

---

## 4. 验证计划

1. **构建与类型验证**：`npm --prefix frontend run build` 确保 `vue-tsc` 与 `vite build` 0 警告 0 报错；
2. **测试验证**：`npm --prefix frontend test` 确保已有前端单元测试全部 PASS；
3. **功能交互验证**：
   - 点击折叠按钮：侧边栏宽度从 260px 缩小至 64px，Logo 文字隐藏，菜单项折叠为图标；
   - 鼠标悬停在折叠后的菜单项上：正常显示菜单项名称 Tooltip / 浮层；
   - 点击展开按钮：侧边栏丝滑展开回 260px，Logo 文字与菜单文字恢复；
   - 刷新页面：localStorage 正确保持之前的折叠/展开状态；
   - 缩放窗口至移动端（<768px）：自动降级为移动端抽屉模式，恢复正常移动端表现。
