# AI Trace 详情页布局重构设计说明书 (Trace Detail Drawer Redesign)

## 意图与动机

Minefolio 的 AI 追踪功能 (`AiTraces.vue` + `AiTraceDetail.vue`) 负责展示每一次 AI 对话与工具调用的底层细节（模型、Prompt、Completion、Token 消耗、延迟、首 Token 耗时、生成速率 tok/s 等）。

然而，现有的 `AiTraceDetail.vue` 采用了传统的居中 `800px` 固定宽度的 `el-dialog` 弹窗：
1. **纵向信息堆叠拥挤**：基础信息、错误信息、输入消息、输出内容全部纵向陈列在单个滚动容器中；
2. **长文本内嵌小滚动条**：输入与输出被强行限制在 `max-height: 300px/400px` 的狭小 `<pre>` 代码块内，在长对话或长分析报告场景下出现两层嵌套滚动条，阅读非常别扭；
3. **输入与输出缺乏对照**：用户排查提示词生效情况时，无法将 User Prompt 与 Assistant Response 并排对比；
4. **关键指标展示不全**：缺少单次调用预估费用 (`cost_usd`)、Top P 等指标，且输出只支持纯代码块展示，不支持富文本 Markdown 解析预览。

本次重构借鉴工业级 LLM 可观测性平台（如 LangSmith / Langfuse / Helicone）的成熟实践，将 `AiTraceDetail.vue` 升级为现代化的**右侧宽屏滑动抽屉 (Slide-over Drawer)**，配备顶部 Bento 指标带、双栏独立滚动工作区、Markdown 渲染与原始纯文本一键切换、一键复制等能力。

---

## 目标

1. **宽屏沉浸式工作区**：将居中狭窄弹窗升级为右侧滑出抽屉 (`el-drawer`)，默认宽度 70%（响应式断点自适应），并支持「全屏展开 / 恢复分栏」一键切换。
2. **顶部 Bento 性能与成本指标带**：清晰直观呈现延迟（Total Latency & 首字 TTFT）、Token 细分（Prompt / Completion / Total）、生成速率（tok/s）、预估成本（USD）及状态徽章。
3. **分栏独立滚动工作台**：
   - 宽屏下（≥992px）提供「输入上下文 (Prompt)」与「模型输出 (Completion)」**左右并列分栏**，各自独立纵向滚动，高度撑满抽屉视口；
   - 窄屏下（<992px）自适应堆叠为流式上下排列。
4. **输出富文本与代码双模式**：模型输出支持「Markdown 渲染视图」与「原始纯文本」一键切换。
5. **快捷操作与原始报文审计**：
   - 支持单条消息独立复制、输入上下文全量复制、输出全量复制；
   - 提供「原始报文 (Raw JSON)」与「调用参数 (Parameters)」标签页，支持格式化高亮与完整导出。

---

## 非目标

1. 不修改后端数据表结构或 API（`AiTraceDetail` 现有字段已完全满足：`input_messages`, `output_content`, `system_prompt`, `latency_ms`, `first_token_ms`, `tokens_per_sec`, `cost_usd`, `prompt_tokens`, `completion_tokens`, `total_tokens`, `temperature`, `top_p`, `metadata` 等）。
2. 不改变 `AiTraces.vue` 列表页的数据加载逻辑与接口调用方式。
3. 不引入额外的重型第三方库，优先复用现有 Element Plus、Iconify 及 Vue 3 内置响应式生态。

---

## 布局与组件结构

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│  Trace #1042  ·  deepseek-chat (deepseek)     [成功]               [全屏展开]  [复制JSON]  [✕] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  ⚡ 延迟: 1,280ms (TTFT 340ms)   🏷️ Token: 1,842 (输入 420 / 输出 1,422)   💰 成本: $0.0023    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  [ 对话与输出 (Messages & Output) ]    [ 原始报文 (Raw Payload) ]    [ 运行参数 (Parameters) ]  │
├───────────────────────────────────────────┬────────────────────────────────────────────┤
│  📥 输入上下文 (3 Messages)         [全部复制] │  📤 模型回复 (Completion)       [Markdown/源码] [复制]│
│                                           │                                            │
│  [SYSTEM]                                 │  根据您的账本数据统计：                     │
│  你是一个资深的个人财务分析师...          │                                            │
│                                           │  ### 1. 资产变动总览                       │
│  [USER]                                   │  - 上月总收入：¥28,450.00                  │
│  请帮我分析一下上个月的支出构成情况...    │  - 上月总支出：¥14,210.00                  │
│                                           │  - 净结余：¥14,240.00                      │
│                                           │                                            │
│  (支持独立纵向滚动，无嵌套卡顿)            │  (支持 Markdown/表格渲染与一键纯文本复制)  │
└───────────────────────────────────────────┴────────────────────────────────────────────┘
```

---

## 关键技术细节

1. **抽屉组件选型**：
   - 使用 `<el-drawer>`，设置 `direction="rtl"`，`destroy-on-close`，动态计算 `:size="isMaximized ? '100%' : '72%'"`。
2. **输入上下文解析与格式化**：
   - 提取 `detail.input_messages`（JSON 数组）及独立的 `detail.system_prompt`。
   - 消息角色区分徽章：System (紫)、User (蓝)、Assistant (绿)、Tool (琥珀)。
3. **输出富文本渲染**：
   - 复用 `marked` 与 `dompurify`（前端已内置该依赖），渲染排版优雅的标题、列表、金融表格与代码块；
   - 切换为源码模式时，显示等宽字体高亮区块。
4. **主题与暗色适配**：
   - 完全遵循 Minefolio `--mf-*` 主题变量（`--mf-surface`, `--mf-background`, `--mf-border`, `--mf-primary`, `--mf-text-main` 等）。
