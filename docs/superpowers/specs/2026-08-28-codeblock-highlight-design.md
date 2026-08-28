# Chat 代码块语法高亮与组件化设计规范 (CodeBlock Component Design)

## 1. 概述与目标

在 Minefolio AI 对话聊天（Chat）中，将 Markdown 代码块全面升级为独立的 `CodeBlock.vue` 组件，集成 `highlight.js` 语法高亮引擎，提供代码语言标头、一键复制代码、暗黑科技主题配色等完整交互体验。

---

## 2. 核心架构与功能设计

### 2.1 组件设计：`frontend/src/components/CodeBlock.vue`
1. **顶部操作栏（Header）**：
   - 左侧展示语言标签（如 `JSON`, `TYPESCRIPT`, `PYTHON`, `C`, `SQL`, `BASH`），大写徽章展示；
   - 右侧提供“一键复制”按钮，使用 `@iconify/vue` 图标（`ph:copy` 与 `ph:check`），复制成功后展示 2 秒绿色反馈提示（“已复制”）；
2. **语法高亮渲染**：
   - 引入 `highlight.js`；
   - 自动识别语言，若为未指定或不支持的语言，安全降级至 `plaintext` 或自动检测；
   - 结果通过 `computed` 缓存，避免流式输出或父组件更新时重复高亮运算；
3. **样式与主题适配**：
   - 配色采用深色科技风（与整体 Slate/Dark 风格一致，背景 `#020617`，边框 `rgba(0, 212, 255, 0.12)`）；
   - 自定义 code block 语法 token 配色（关键字 Cyan/Purple，字符串 Green/Amber，注释 Muted Gray）；
   - 横向支持 `overflow-x: auto` 滚动条，等宽字体 `var(--mf-font-mono)`。

### 2.2 消息解析：`frontend/src/components/ChatMessageContent.vue`
1. **段落拆解与匹配**：
   - 升级正则表达式，将所有代码块（` ```lang ... ``` `）从纯 Markdown 文本中提取为独立段落对象（`type: 'code'`）；
   - 保留原有的 `mermaid` 与 `action` 自定义块逻辑；
2. **流式传输兼容**：
   - 针对未闭合的代码块（输入进行中），实时展示语法高亮与脉冲动效。

---

## 3. 依赖项与文件改动

1. **依赖库**：`highlight.js`（npm package）；
2. **新建文件**：[`frontend/src/components/CodeBlock.vue`](file:///data/home/quintin/workspace/source/c/Minefolio/frontend/src/components/CodeBlock.vue)；
3. **修改文件**：[`frontend/src/components/ChatMessageContent.vue`](file:///data/home/quintin/workspace/source/c/Minefolio/frontend/src/components/ChatMessageContent.vue)。

---

## 4. 验证计划

1. **构建与类型验证**：`npm --prefix frontend run build`（`vue-tsc -b` 0 报错）；
2. **单元测试验证**：`npm --prefix frontend test`（测试套件 100% 通过）；
3. **功能验证**：
   - 包含 C/Python/JSON/TS 代码块的 AI 消息能正确着色；
   - 点击“复制”按钮，剪贴板成功写入纯代码内容并显示复制成功状态；
   - 未闭合代码块在流式传输时不发生崩溃。
