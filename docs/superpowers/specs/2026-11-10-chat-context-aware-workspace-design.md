# AI 对话上下文感知欢迎看板与会话智能置顶设计规范

**日期**: 2026-11-10  
**模块**: `frontend/src/views/Chat.vue`, `frontend/src/components/PromptStarters.vue`, `frontend/src/stores/chat.ts`  
**状态**: 待评审 / Design Draft  

---

## 1. 现状与业务痛点 (Intent & Why)

1. **新开会话缺乏上下文感知 (Cold Start)**：
   - 当前会话空白页（`PromptStarters.vue`）仅展示 5 张完全静态的提示词卡片。
   - 用户打开对话页面时，AI 无法主动感知用户当前的实际财务状况（如净资产、负债率、最近是否有高额开支），用户仍需手动在脑海中回忆或打字向 AI 描述，缺乏“懂我财务状况的贴心智能管家”专属感。
2. **侧边栏历史会话无序堆叠 (Session Clutter)**：
   - 侧边栏仅按时间（今天、昨天、近7天...）简单分组，随着对话增多，重要的长线理财规划、资产体检会话极易被临时单次记账冲散埋没；
   - 缺少会话置顶（Pin）机制；
   - 缺乏单会话级别的快捷操作面板（如导出指定单条会话为 Markdown、快速复制、场景分类标签）。

---

## 2. 目标与非目标 (Goals & Anti-Goals)

### 目标 (Goals)
1. **动态资产感知欢迎看板**：
   - `PromptStarters.vue` 异步加载当前账本核心财务指标（`summaryApi.get()`），在欢迎看板上方呈现轻量“**财务全景速览胶囊**”（净资产、总资产、负债率）；
   - 根据真实财务状态动态生成 2~3 个高价值洞察卡片（如负债诊断、权益类资产结构诊断），点击卡片自动将包含真实数值的个性化 Prompt 填入/发送；
   - 在接口加载中或离线/失败时，平滑降级为经典通用提示词，保证首屏 0 白屏 0 阻塞。
2. **会话置顶常驻管理 (Session Pinning)**：
   - `chat.ts` 新增 `pinnedSessionIds`，基于 LocalStorage 持久化（与现有 `pinnedWorkflowIds` 架构保持统一），支持跨会话记忆；
   - `Chat.vue` 侧边栏若存在置顶会话，在最顶部呈现独立的“📌 置顶会话”分组；
   - 会话条目呈现专属图钉标识，鼠标悬浮时提供一键置顶/取消置顶按钮。
3. **单会话快捷操作菜单 (Context Menu / More Dropdown)**：
   - 会话项提供 `...` 操作菜单，支持：置顶/取消置顶、单会话导出为 Markdown、重命名、删除会话。
4. **会话场景智能徽章 (Smart Scenario Tags)**：
   - 侧边栏根据会话标题与内容关键词（如“体检”、“记账”、“调拨”、“定投”、“汇率”）自动计算场景徽章（如 `📊 资产体检`、`🧾 记账调拨`、`📈 行情测算`），一眼识别会话主题。

### 非目标 (Non-Goals)
- 本期暂不引入复杂的服务端文件夹层级目录树（避免侵入 C 语言 DDD 仓储层与多表联查）。
- 本期暂不改变后端的 session 数据表结构（纯前端驱动，零迁移风险）。

### 反目标 (Anti-Goals)
- **绝不阻塞页面加载**：财务概览异步拉取失败或超时不能影响聊天会话创建与输入框可用性。
- **绝不破坏现有测试契约**：保持原有会话切换、消息发送、重命名与删除的所有逻辑 100% 兼容。

---

## 3. 方案选型与权衡 (Approaches)

### 方案 A：轻量前端感知与会话本地置顶（选定）
- **实现方式**：
  - `PromptStarters.vue` 在 `onMounted` 钩子中异步拉取 `summaryApi.get()`；
  - `chat.ts` 中维护 `pinnedSessionIds: Ref<number[]>` 并落盘 LocalStorage（键名 `minefolio_pinned_sessions`）；
  - `Chat.vue` 在计算属性 `groupedSessions` 中优先提取置顶会话放入第一组。
- **权衡优势**：纯前端实现，零后端数据库迁移风险，离线/弱网鲁棒性好，代码高内聚且完全符合现有状态机设计。

### 方案 B：服务端全持久化与会话文件夹归档（未选定）
- **权衡原因**：需要新建 `V021` 数据库迁移给 `ai_sessions` 表添加 `is_pinned` 字段并修改 C 语言后端控制器，改动跨度大且对单机个人自托管场景带来不必要的复杂度。

---

## 4. 详细模块设计 (Detailed Design)

### 4.1 财务上下文感知欢迎看板 (`PromptStarters.vue`)
- **资产速览胶囊 (`.context-summary-capsule`)**：
  - 展示 3 组关键数据项：
    - `净资产`: 格式化金额（如 `¥128,500.00`）
    - `总资产`: `¥150,000.00`
    - `资产负债率`: `14.3%`（若总负债为 0，展示“零负债”）
  - 右侧状态小标：“已连通实时账本 · 智能分析就绪”。
- **动态洞察推荐生成算法 (`dynamicStarters`)**：
  - **规则 1（负债感知）**：若 `total_liabilities > 0` 且负债率 > 15%，生成《高负债健康度评估与利息压降建议》，附带真实负债数值与优化提问；
  - **规则 2（资产结构）**：若 `total_assets > 0`，生成《核心资产分布与抗波动韧性诊断》，带入当前净资产数值让 AI 计算流动性备用金；
  - **规则 3（零资产新用户）**：若 `total_assets === 0`，生成《从零开始：个人首张资产负债表搭建指南》；
  - **经典底座**：保留 2~3 个经典快捷记账与行情测算模板，确保布局均衡饱满。

### 4.2 会话状态存储增强 (`frontend/src/stores/chat.ts`)
- 新增状态与方法：
  ```ts
  const PINNED_SESSIONS_KEY = 'minefolio_pinned_sessions'
  const pinnedSessionIds = ref<number[]>(loadPinnedSessions())

  function isSessionPinned(sessionId: number): boolean {
    return pinnedSessionIds.value.includes(sessionId)
  }

  function togglePinSession(sessionId: number) {
    const idx = pinnedSessionIds.value.indexOf(sessionId)
    if (idx >= 0) {
      pinnedSessionIds.value.splice(idx, 1)
    } else {
      pinnedSessionIds.value.push(sessionId)
    }
    localStorage.setItem(PINNED_SESSIONS_KEY, JSON.stringify(pinnedSessionIds.value))
  }
  ```

### 4.3 侧边栏布局与快捷菜单重构 (`frontend/src/views/Chat.vue`)
- **分组计算属性 `groupedSessions`**：
  - 过滤掉当前搜索关键词匹配的会话后：
    1. 提取 `isSessionPinned(s.id)` 为 `true` 的会话，放入顶部分组 `{ label: '📌 置顶会话', isPinnedGroup: true, items: [...] }`；
    2. 其余会话按原逻辑时间分组（今天、昨天、近7天、近30天、更早）。
- **场景徽章判定逻辑与表现 (Builder Call)**：
  - **判定数据源**：仅根据会话标题 `s.title` 进行纯前端轻量关键词正则匹配，**绝不额外发送消息请求**，保证 0 额外网络开销与无任何 N+1 渲染卡顿；
  - **视觉呈现**：仅在命中特定财务主题时在标题左侧附带精致 Emoji/微图标（`体检|健康|诊断` -> 📊，`记账|消费|支出|调拨` -> 🧾，`行情|汇率|定投|复利` -> 📈）；未命中的常规会话保持原样纯文本排版，不强行填充 `💬`，避免侧边栏产生视觉噪点并保持标题可读宽度。
- **会话项渲染与微交互**：
  - 会话项右上角若已置顶显示微型图钉图标 `ph:push-pin-fill`；
  - 悬浮操作栏增加：
    - 置顶/取消置顶按钮（带有 Tooltip 提示）；
    - 更多操作下拉菜单 `el-dropdown`（包含：置顶切换、导出为 Markdown、重命名、删除）；
- **单会话导出为 Markdown (Builder Call)**：
  - 若为当前会话：直接根据已有消息调用现有的 `exportToMarkdown()`；
  - 若为其他历史会话：拉取该会话消息（`getMessages(id, 1, 100)`）格式化并触发文件下载，操作期间提供轻量 Toast 提示。

---

## 5. 交付物与测试计划

1. **测试用例**：
   - `frontend/tests/chat-session-pin.spec.ts`：测试置顶状态切换、LocalStorage 落盘与 `groupedSessions` 置顶分组排列；
   - `frontend/tests/prompt-starters-context.spec.ts`：测试 `summaryApi.get()` 异步注入、胶囊展示与动态洞察 Prompt 生成；
   - 回归已有全部 197 个单元测试。
2. **构建验证**：
   - `npm --prefix frontend run build` 确保 0 TypeScript 报错。
