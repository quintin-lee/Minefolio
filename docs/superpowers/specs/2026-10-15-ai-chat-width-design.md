# AI 对话窗口动态宽度设计

日期: 2026-10-15
范围: `frontend/src/views/Chat.vue` 纯 CSS 改动，无 TS/逻辑变更
状态: 已实现并通过 `npm run build`

## 1. 问题

`Chat.vue` 的消息阅读列 `.messages` 与悬浮输入岛 `.chat-input-area`
写死 `max-width: 860px`。宽屏下（全局侧栏收起 260px→64px、
会话侧栏收起 280px→0 时，可用宽度增加约 200–480px）中央列不变，
两侧留白过大；视口媒体查询感知不到侧栏折叠，宽度与页面实际可用空间脱节。

## 2. 候选方案

| 方案 | 说明 | 取舍 |
|---|---|---|
| A. 视口断点阶梯 (`@media min-width`) | 按窗口宽度设 860 / 960 / 1080px | 简单，但侧栏折叠不改变视口，无法响应页面真实变化 ❌ |
| B. 容器查询流式宽度（采用） | `.chat-main` 设为 container，列宽 `min(1060px, 100cqi)`，超宽容器放宽到 `min(1120px, 96cqi)` | 真正跟随页面可用宽度；需现代浏览器，老浏览器走 860px 回退 ✅ |
| C. 去掉 max-width 全宽 | 阅读行过长，可读性差 ❌ | 否决 |

## 3. 实现（仅 CSS，无逻辑改动）

- `.chat-main` 新增 `container-type: inline-size; container-name: chat-main;`
 （成为查询容器，`cqi` = 其内容区宽度，侧栏折叠/窗口缩放都会改变它）。
- `.messages` / `.chat-input-area`：
  - 保留 `max-width: 860px` 第一声明做老浏览器回退；
  - 追加 `max-width: min(1060px, 100cqi)`：窄页自动撑满不溢出，
    宽页从 860px 放宽到最高 1060px。
- `@container chat-main (min-width: 1400px)` 时两者放宽到
  `min(1120px, 96cqi)`（超宽屏多用一点，两侧保留呼吸感）。
- 移动端（≤768px）追加 `100cqi` 回退，与原有 `100%` 行为一致。

`.message-row.user (82%)` 等相对列宽的规则无需改动，自动跟随。

## 4. 验证

- `npm run build`（vue-tsc + vite）：✓ built in 23.49s，0 错误。
- Vue LSP 未安装（已记录 declined），CSS 改动无类型影响。
- 注意：工作区另有他人未提交的后端改动
 （`backend/CMakeLists.txt`、`mcp_server_repo_impl.c`、`report_asset_service.c`），
  本次未触碰。

## 5. 后续可调参数

- 若觉得宽屏列太宽/太窄，只需调 `1060px / 1120px` 两个上限值。
- 若需更细粒度断点，可在 `@container` 中追加 `1100px / 1600px` 档。
