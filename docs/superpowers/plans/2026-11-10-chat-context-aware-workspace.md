# AI 对话上下文感知欢迎看板与会话智能置顶实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 AI 对话空白页的动态财务资产感知看板与侧边栏会话智能置顶管理，使 AI 具备即时的财务上下文，同时让高价值历史理财会话一键常驻置顶。

**Architecture:** 
1. 状态层：在 Pinia `chat.ts` 中维护 `pinnedSessionIds`，基于 `localStorage` (`minefolio_pinned_sessions`) 进行本地持久化，跨会话即时保持置顶状态。
2. 侧边栏：`Chat.vue` 在会话分组计算属性中自动将置顶会话提取为顶部的“📌 置顶会话”专区，提供图钉悬浮快捷键、`...` 快捷菜单（含单会话 Markdown 导出、重命名、删除）以及基于标题的场景微图标。
3. 欢迎看板：`PromptStarters.vue` 挂载时异步拉取当前账本核心财务指标（`summaryApi.get()`），渲染资产概览速览胶囊，并基于用户真实资产与负债结构动态注入个性化智能洞察提示词，接口失败时平滑降级。

**Tech Stack:** Vue 3, TypeScript, Pinia, Element Plus, Iconify Icons, Vitest.

**Spec:** [`docs/superpowers/specs/2026-11-10-chat-context-aware-workspace-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-10-chat-context-aware-workspace-design.md)

## Global Constraints

- **零后端依赖与迁移风险**：不修改后端数据库表与接口，纯前端状态与接口调用实现。
- **构建零错误**：`npm --prefix frontend run build`（vue-tsc -b && vite build）必须 0 错误。
- **全量测试通过率 100%**：所有新旧单元测试全部通过（原 197+ 测试无回归）。
- **非阻塞式异步拉取**：财务指标拉取超时或失败绝不阻塞页面交互或产生控制台未捕获错误。

## Review Focus

1. **LocalStorage 异常健壮性**：隐私模式或 `localStorage` 不可用/脏数据时自动降级为空数组，不抛出异常破坏整个应用。
2. **搜索联动下的置顶会话展示**：当用户在侧边栏输入搜索关键词时，置顶分组内的会话同样接受搜索词过滤，无匹配时不展示多余的置顶组头。
3. **欢迎页资产数据为 0 或离线时的平滑降级**：新注册用户（资产与负债均为 0）或网络断开时，看板必须展示合理的搭桥建议或经典模板，禁止出现 `NaN%` 或空白卡片。
4. **非当前会话 Markdown 导出的隔离性**：导出非当前激活会话的消息时，绝不能篡改当前 Pinia 中正在对话的 `chat.messages` 状态。
5. **主题色彩兼容**：置顶标识高亮、概览胶囊底色及更多操作菜单在浅色与深色主题下文字与边框对比度清晰。

---

### Task 1: Pinia Chat Store 会话置顶状态持久化与测试契约 (TDD)

**Files:**
- Modify: `frontend/src/stores/chat.ts`
- Modify: `frontend/tests/chat.store.spec.ts`

**Interfaces:**
- Consumes: `localStorage`
- Produces:
  - `pinnedSessionIds: Ref<number[]>`
  - `togglePinSession(sessionId: number): void`
  - `isSessionPinned(sessionId: number): boolean`

- [ ] **Step 1: 编写置顶状态管理的测试用例 (RED)**

在 `frontend/tests/chat.store.spec.ts` 中增加测试套件：
1. 验证初始状态可从 `localStorage` 读取已置顶 ID 数组；
2. 验证调用 `togglePinSession` 能在列表中新增/移除指定会话 ID，并同步写入 `localStorage`；
3. 验证 `isSessionPinned` 正确返回布尔值。

- [ ] **Step 2: 运行测试以验证失败**

运行: `npm --prefix frontend test -- tests/chat.store.spec.ts --run`  
预期: FAIL（`togglePinSession` / `isSessionPinned` 未定义）

- [ ] **Step 3: 在 `frontend/src/stores/chat.ts` 中实现置顶逻辑 (GREEN)**

1. 定义 `PINNED_SESSIONS_STORAGE_KEY = 'minefolio_pinned_sessions'`；
2. 定义响应式变量 `pinnedSessionIds = ref<number[]>(loadPinnedSessions())`；
3. 实现 `togglePinSession(id: number)` 与 `isSessionPinned(id: number)`，并在 `useChatStore` 的 return 对象中导出。

- [ ] **Step 4: 运行测试以验证通过**

运行: `npm --prefix frontend test -- tests/chat.store.spec.ts --run`  
预期: PASS。

- [ ] **Step 5: 提交 Task 1 代码**

```bash
git add frontend/src/stores/chat.ts frontend/tests/chat.store.spec.ts
git commit -m "feat(chat): ⚡ add session pinning state management with local persistence"
```

---

### Task 2: 侧边栏置顶分组、场景徽章与单会话操作菜单 (TDD)

**Files:**
- Modify: `frontend/src/views/Chat.vue`
- Create: `frontend/tests/chat-session-pin.spec.ts`

**Interfaces:**
- Consumes: `chat.pinnedSessionIds`, `chat.togglePinSession`, `chat.isSessionPinned`, `getMessages`
- Produces:
  - 侧边栏计算属性 `groupedSessions` 的“📌 置顶会话”置顶分组
  - 场景微图标计算函数 `getSessionTag(title: string): string | null`
  - 会话项操作栏中的置顶图钉按钮与 `...` 下拉菜单（支持单会话 Markdown 导出）
  - 单会话导出函数 `handleExportSingleSession(s: AiSession)`

- [ ] **Step 1: 编写侧边栏置顶与场景图标单元测试 (RED)**

创建 `frontend/tests/chat-session-pin.spec.ts`：
1. 测试当存在置顶会话时，`groupedSessions` 第一个分组为 `📌 置顶会话`，且仅包含置顶项；
2. 测试未置顶会话依然落入时间周期分组；
3. 测试会话标题命中“体检”、“记账”、“行情”时渲染对应 Emoji，普通标题不展示 Emoji；
4. 测试点击置顶/取消置顶按钮触发 `chat.togglePinSession`。

- [ ] **Step 2: 运行测试验证失败**

运行: `npm --prefix frontend test -- tests/chat-session-pin.spec.ts --run`  
预期: FAIL。

- [ ] **Step 3: 在 `frontend/src/views/Chat.vue` 中实现侧边栏置顶与菜单增强 (GREEN)**

1. 在脚本中引入 `chat.isSessionPinned` 与 `chat.togglePinSession`；
2. 重构 `groupedSessions` 计算属性：先分离置顶会话（经搜索词过滤后），若非空则创建 `{ label: '📌 置顶会话', isPinnedGroup: true, items: [...] }` 放入数组首位；其余会话按时间划分；
3. 实现 `getSessionTag(title: string)`：正则匹配关键词返回 `📊`、`🧾`、`📈` 或 `null`；
4. 在模板 `.session-item` 中：
   - 标题左侧条件渲染场景徽章；
   - 标题右侧若是置顶项展示微型 `ph:push-pin-fill` 图标；
   - 悬浮操作栏中增加图钉按钮（`ph:push-pin` / `ph:push-pin-slash`）与更多 `el-dropdown`（置顶切换、导出 Markdown、重命名、删除）；
5. 实现 `handleExportSingleSession(s: AiSession)`：
   - 若 `s.id === chat.currentSessionId`，直接调用 `exportToMarkdown()`；
   - 否则异步调用 `getMessages(s.id, 1, 100)` 获取消息数组，复用 Markdown 格式化并触发文件下载。

- [ ] **Step 4: 运行测试验证通过**

运行: `npm --prefix frontend test -- tests/chat-session-pin.spec.ts --run`  
预期: PASS。

- [ ] **Step 5: 提交 Task 2 代码**

```bash
git add frontend/src/views/Chat.vue frontend/tests/chat-session-pin.spec.ts
git commit -m "feat(chat): ⚡ add pinned sessions group, scenario badges and session dropdown menu"
```

---

### Task 3: 动态资产感知欢迎看板与个性化建议生成 (TDD)

**Files:**
- Modify: `frontend/src/components/PromptStarters.vue`
- Create: `frontend/tests/prompt-starters-context.spec.ts`

**Interfaces:**
- Consumes: `summaryApi.get()` from `@/api/summary`
- Produces:
  - 资产全景速览胶囊 `.context-summary-capsule`（净资产、总资产、资产负债率）
  - 动态洞察建议卡片计算属性 `computedStarters`
  - 点击卡片触发 `@select` 事件带入精准财务上下文 Prompt

- [ ] **Step 1: 编写动态欢迎看板测试用例 (RED)**

创建 `frontend/tests/prompt-starters-context.spec.ts`：
1. Mock `summaryApi.get` 返回资产数据（例如 `net_worth: 120000, total_assets: 150000, total_liabilities: 30000`）；
2. 验证渲染 `.context-summary-capsule`，并展示正确的金额与负债率（`20.0%`）；
3. 验证根据财务数据动态生成包含实际数字的 Prompt 卡片；
4. 验证当 `summaryApi.get` 失败时，依然正常渲染经典备选卡片，无报错。

- [ ] **Step 2: 运行测试验证失败**

运行: `npm --prefix frontend test -- tests/prompt-starters-context.spec.ts --run`  
预期: FAIL。

- [ ] **Step 3: 在 `frontend/src/components/PromptStarters.vue` 中实现动态感知逻辑 (GREEN)**

1. 引入 `summaryApi` 与 `Summary` 类型；
2. 定义 `summaryData = ref<Summary | null>(null)` 与 `loadingSummary = ref(false)`；
3. 在 `onMounted` 异步调用 `summaryApi.get()`，成功后保存数据，失败则默默吞下保持 `null`；
4. 实现 `computedStarters`：
   - 若 `summaryData` 存在：
     - 若负债率 > 15%，生成负债健康度评估卡片；
     - 若总资产 > 0，生成包含当前总资产/净资产数字的全景资产配置建议卡片；
     - 结合经典快捷记账与行情卡片混合展示；
   - 若 `summaryData` 为空，展示经典通用卡片；
5. 在模板头部增加 `.context-summary-capsule`：展示净资产、总资产、负债率与“已连通实时账本”状态点；
6. 补充样式（卡片渐变、暗色/亮色模式自适应）。

- [ ] **Step 4: 运行测试验证通过**

运行: `npm --prefix frontend test -- tests/prompt-starters-context.spec.ts --run`  
预期: PASS。

- [ ] **Step 5: 提交 Task 3 代码**

```bash
git add frontend/src/components/PromptStarters.vue frontend/tests/prompt-starters-context.spec.ts
git commit -m "feat(chat): ⚡ add live financial context capsule and dynamic prompt recommendations"
```

---

### Task 4: 全量回归测试与生产环境打包验证

**Files:**
- Test: `frontend/tests/` 全部测试集

- [ ] **Step 1: 执行所有前端测试套件**

运行: `npm --prefix frontend test -- --run`  
预期: 全部测试用例 PASS（所有 40+ 测试文件、200+ 测试）。

- [ ] **Step 2: 执行前端生产环境打包**

运行: `npm --prefix frontend run build`  
预期: 成功生成 `dist/`，0 TS 报错。

- [ ] **Step 3: 检查 Git 工作区状态**

运行: `git status`  
预期: 工作区干净无残留。
