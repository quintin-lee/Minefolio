# iLink 微信用户级独立实例架构说明

## 1. 概述

每个 Minefolio 用户拥有一个独立的微信 iLink 机器人实例（`im_bot_instances`，`platform='wechat'`, `adapter='ilink'`）：

- 独立微信机器人账号（用户自备专用小号）
- 独立 `bot_token`（永不入库，仅以 `0600` 权限落在网关 `<ILINK_STATE_DIR>/<instance_id>/account.json`）
- 独立 Node 子进程（`gateway/ilink/runner.js`），由 `gateway/ilink/supervisor.js` 对账拉起与回收
- 仅响应实例 owner 本人的微信（`from_user_id === account.ilink_user_id`）且二次由后端校验 `im_user_id === instance.im_user_id`
- AI 对话复用现有 `im_bot_usecase_chat_submit` 与只读工具白名单（`im_chat_task_fn`）

## 2. 状态机

```text
pending_login ──► qr_ready ──► scanned ──► active
                     │            │
                     │            └──► need_verifycode ──(PIN)──► scanned
                     │
                     └──(刷新 > 3 次)──► expired

active ──(iLink -14)──► session_expired ──(App relogin)──► pending_login
any    ──(App disable)──► disabled      ──(App enable)───► pending_login
```

- 需要运行子进程的状态：`pending_login`、`qr_ready`、`scanned`、`need_verifycode`、`active`。
- 停止状态：`session_expired`、`expired`、`error`、`disabled`。

## 3. 安全不变量

1. **Owner 严格隔离**：`/api/im/instances*` 全部按 `mf_user_id = ctx_user_id` 过滤，非 owner 返回 `1003`；`im_bind` 受限码禁止访问 `/api/im/instances*`。
2. **网关服务鉴权**：`/api/gateway/im/*` 跳过 JWT 与 CSRF 中间件，在 handler 入口通过 `gateway_auth_check()` 对 `X-Gateway-Token` 做常量时间比较；未配置 `MINEFOLIO_GATEWAY_TOKEN` 时一律拒绝（`1001`）。
3. **绑定独占**：`im_bot_instances` 对 `(platform, im_user_id)` 与 `(platform, ilink_bot_id)` 建部分唯一索引；`mf_im_bot_instance_activate` 在事务内校验实例与 `im_bot_bindings` 无跨用户占用，否则返回 `1004`。
4. **凭证不出网关**：`bot_token` 只存在于网关本地 state 目录；`qr_content` 仅在登录态返回给 owner；`pending_verify_code` 限时 120s、限次 10 次、一次性消费且永不返回给 App。
5. **非 owner 静默丢弃**：子进程忽略 `message_type === 2`、群聊及非 `account.ilink_user_id` 的私聊消息，不回复也不查库。

## 4. 部署

- 后端注入 `MINEFOLIO_GATEWAY_TOKEN`。
- 网关启动：`MINEFOLIO_GATEWAY_TOKEN=<token> MINEFOLIO_API=http://minefolio:8080 node gateway/ilink-server.js`。
- Docker Compose：`docker compose --profile im up -d`（持久化卷 `ilink_state` 挂载到 `/state`）。
