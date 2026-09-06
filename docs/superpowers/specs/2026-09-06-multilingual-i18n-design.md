# 前端全量多语言（i18n）完善设计

日期：2026-09-06
状态：已评审

## 背景与现状

前端已存在 vue-i18n 基础设施，但覆盖面很小：

- `src/composables/useI18n.ts`：注册 vue-i18n（`legacy:false`），`zh-CN` 默认 / `en-US` fallback，语言持久化到 `localStorage.minefolio_lang`；导出 `useI18n()`（含 `setLocale`/`initLocale`）。
- `src/main.ts` 已注册 i18n；`main-mobile.ts` **未注册**。
- Element Plus 语言固定为 zh-cn（`app.use(ElementPlus, { locale: zhCn })`），**不随语言切换**。
- 语言切换控件已存在于「设置 → 外观」（`AppearanceSettings.vue`，zh/en radio）。
- 已迁移文件仅约 9 个：`Layout.vue`、`Login.vue`、`Settings.vue` + `components/settings/*`（7 个子页）。
- 仍存在**第二套翻译系统** `src/utils/locale.ts`：自带字典查找的 `t()`，与 vue-i18n 并存，易漂移。
- 未迁移面：17 个桌面视图 + 11 个移动视图 + 40 个组件，约 **1,700+ 条中文静态文案**硬编码在 `.vue` 中。

## 目标（Scope）

- **全量覆盖**：桌面端视图、共享组件、移动端视图的用户可见静态文案，全部走 i18n（zh-CN / en-US）。
- 语言切换后：应用文案、Element Plus 组件文案（日期选择器、分页、空态等）一致切换。
- **仅翻译前端静态文案**：后端接口返回的 toast / 校验消息保持后端原文（中文为主），不在本期做后端多语言。
- 用户数据（分类名、账户/账本名、备注、AI 生成内容等）**不翻译**。

## 非目标（Non-Goals）

- 后端（C 层）多语言消息。
- AI 生成内容翻译。
- 币种符号/金额单位按 locale 本地化（CNY 为主，不属于语言切换范畴）。
- 数据库内种子/用户数据的迁移翻译。
- 桌面与移动视图合并（既有重构计划，与本任务无关）。

## 决策记录

1. 覆盖范围：桌面 + 移动端全量。
2. 后端消息：保持原文，仅翻译前端静态文案。
3. 迁移方式：按功能批次手动迁移（每批可独立提交、可验证）。
4. 语言集：仅 `zh-CN`（默认）与 `en-US`。

## 架构变更

### 1. Element Plus 语言响应式切换

`App.vue`（桌面）与移动布局中：

- 用 `<el-config-provider :locale="epLocale">` 包裹应用内容。
- `epLocale` = 由 vue-i18n `locale` 计算：`zh-CN → zh-cn`、`en-US → en`（`element-plus/es/locale/lang/*`）。
- `html[lang]` 同步切换（`zh-CN` / `en-US`）。

### 2. 移动端接入

- `main-mobile.ts`：注册 i18n，启动时执行 `initLocale()`（与桌面一致）。
- 移动端页头加紧凑语言切换（`中 / EN`），复用 `minefolio_lang` 持久化。

### 3. 统一翻译系统

- `utils/locale.ts`：删除自带字典查找逻辑，改为薄封装 vue-i18n 的全局 `t`（`i18n.global.t(key)`，调用时读取当前 locale，保持现有导入点可用），随后现有调用点迁移到 `$t`/composable 后删除。
- 词典唯一来源：`src/locales/zh-CN.ts` 与 `src/locales/en-US.ts`。

### 4. 数字分组随语言

- `utils/format.ts` 的 `Intl.NumberFormat` 区域参数从固定 `'zh-CN'` 改为读取当前语言（`en-US → 'en-US'`，其余 `'zh-CN'`）；币种仍为 CNY。

## 词典约定

- 命名空间按功能划分：现有 `nav / common / login / settings.*`，新增 `dashboard.* / assets.* / holdings.* / transactions.* / dailyExpenses.* / plans.* / categories.* / transfer.* / reports.* / audit.* / aiTraces.* / chat.* / setup.* / oauth.* / viewsMobile.* / components.*`（实现时可细化，无需预先穷举）。
- 动态值用 vue-i18n 插值 `{param}`；**两种语言同一 key 的插值参数必须一致**（测试守卫）。
- 客户端“代码→中文标签”映射表（如 `asset_type`、`tx_type`、状态 chip）改为词典 key；来自后端/DB 的名称不迁移。

## 迁移批次（每批独立提交）

1. **基础设施**：EP `config-provider` 响应式；`main-mobile` 注册 i18n + 移动端语言切换；`utils/locale` 统一到 vue-i18n；`format.ts` locale 感知分组；新增 i18n key 对齐单测。
2. **外壳**：Layout/导航/登录/Settings 及全部 settings 子页收尾（补齐英文、与字典一致）。
3. **桌面视图**（按路由组分若干子批）：资产组（Dashboard/Assets/Holdings/Transactions）、收支组（DailyExpenses/Categories/Transfer/Plans/Reports）、AI 组（Chat + 工作流组件、AiTraces/AiTraceDetail）、系统组（Setup/登录/OAuth 收尾等）。
4. **共享组件**：弹窗、表格、图表、工作流/斜杠菜单/Mermaid/Action 卡片等扫尾。
5. **移动端视图**：11 个 views-mobile + 页头语言切换。
6. **对齐与打磨**：en/zh key 对齐测试全绿；Playwright 英文抽检；残留中文扫描（仅允许数据/品牌/转写例外）；文档更新（README/AGENTS）。

## 验证

- 每批：`vue-tsc -b`（`npm run build` 桌面 + `build:mobile` 移动）。
- 新增 vitest `tests/i18n.keys.spec.ts`：
  - en 与 zh 的 key 树完全一致（双向递归比较）；
  - 任一语言缺失插值参数（`{xxx}` 不一致）即失败；
  - 词典结构类型检查（zh 为基准，en 完整）。
- 最终：Playwright 英文抽检（登录、仪表盘、交易、设置、一个弹窗/抽屉，基于桌面 dev 栈），确认关键 UI 无残留中文文案；移动端验证方式为 `build:mobile` 构建通过 + i18n key 对齐测试 + 残留中文静态扫描（覆盖同一批词典）。桌面 + 移动构建均通过。

## Definition of Done

- `.vue` 模板/脚本中的用户可见静态中文全部替换为 i18n key（例外：品牌名 Minefolio、纯数据渲染、动态用户内容）。
- en-US 词典与 zh-CN key 100% 对齐，无缺 key / 无缺插值参数。
- 语言切换后 EP 组件文案与 `html[lang]` 同步。
- 移动端可切换语言且构建通过。
- `npm run build`、`npm run build:mobile`、`npm test` 全绿。
