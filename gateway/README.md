# Minefolio IM 网关 (gateway/)

桥接 QQ / 微信消息到后端只读查账端点 `/api/im/*`。零外部依赖，仅用 Node ≥ 18 内置 `fetch`。

## 文件

| 文件 | 职责 |
|------|------|
| `index.js` | 网关核心：`handleImMessage(platform, imUserId, text, bindToken)` + `/bind` / `/bind <user> <pass>` 处理 + `im_bindings.json` 持久化 + `refreshBindToken` |
| `cli.js` | CLI 模拟 IM 用户，跑通「绑定 → 查账 → 写操作收敛」闭环（不依赖真实 QQ/微信 运行时） |
| `package.json` | 零依赖，`npm start` / `npm run cli` |

## 数据流

```
IM 框架 (NapCat/OneBot | wechaty | cli.js)
   │  收消息 handleImMessage(platform, imUserId, text, bindToken)
   ▼
gateway/index.js
   │  /bind <code>  → POST /api/im/{platform}/bind   (code 作 Bearer)
   │  其他文本      → POST /api/im/{platform}/chat    (bindToken 作 Bearer)
   ▼
backend /api/im/*  (P0: 只读工具白名单 + 抢占防护 + bound==user 一致性校验)
```

## 最小闭环（CLI，无真实 IM 运行时）

```bash
# 1. 起后端 (配好 AI 模型, 让只读查账 LLM 可用)
./backend/build/minefolio            # 默认 :8080

# 2. App 侧拿绑定码 (此处用 curl 模拟; 真实场景是 App「IM 机器人」卡片)
APP_TOKEN=$(curl -s -X POST -H 'Content-Type: application/json' \
  http://localhost:8080/api/auth/login -d '{"username":"u","password":"p"}' | jq -r .data.token)
BIND=$(curl -s -X POST -H "Authorization: Bearer $APP_TOKEN" \
  http://localhost:8080/api/im/bindings/token | jq -r .data.bind_token)

# 3. CLI 模拟 QQ 用户
node gateway/cli.js qq 12345 "$BIND" "/bind $BIND"        # 绑定
node gateway/cli.js qq 12345 "$BIND" "我最近一个月花了多少"  # 只读查账
node gateway/cli.js qq 12345 "$BIND" "帮我记一笔 500 吃饭"   # 写操作 → 只读收敛, 不产生动账
```

## QQ / 微信 适配（P1，本仓库已实现核心桥接）

`gateway/adapters/` 提供 transport-agnostic 适配器，复用 `handleImMessage`：

| 文件 | 职责 |
|------|------|
| `adapters/onebot.js` | OneBot v11 协议桥接（通吃 NapCat/LLObot/go-cqhttp）；可注入 `getBindToken` + `transport`，只处理私聊 |
| `adapters/onebot-server.js` | 可运行 QQ 服务：连 OneBot WS 收事件，HTTP `send_private_msg` 回复（零依赖，仅 `ws` 可选） |
| `adapters/wechaty.js` | wechaty 桥接（可选依赖 `npm i wechaty <puppet>`），1:1 只读 |
| `adapters/mock-events.js` | 测试用 mock transport + OneBot 事件构造 |

### 启动 QQ 网关（NapCat）

```bash
cd gateway
# NapCat 开 OneBot: 3001 端口 (WS + HTTP)
export ONEBOT_WS_URL=ws://127.0.0.1:3001
export ONEBOT_API_URL=http://127.0.0.1:3001
export ONEBOT_ACCESS_TOKEN=...            # NapCat access token
export ONEBOT_BIND_TOKEN=<某用户的 im_bind 码>   # 单一服务码 (P1)
# 或按 uid 区分:
export ONEBOT_BIND_MAP='{"12345":"<bind_token>"}'
npm run onebot
```

除静态配置（`ONEBOT_BIND_TOKEN`/`ONEBOT_BIND_MAP`）外，现已支持通过后端 `/api/im/{platform}/bind-auth` 实现 IM 端账号密码自助绑定，并将绑定关系持久化至 `im_bindings.json`（已开启 2FA 的账号需回到 App 生成一次性绑定码进行绑定）。

### 测试（不连真 IM）

```bash
cd gateway
npm run test:adapters      # node --test adapters/test/  (11 用例: 桥接/群忽略/wechaty guard/缓存)
```


绑定码管理（生产）：
- `refreshBindToken(appToken)` 可周期刷新某用户的 300s 绑定码；
- 更稳的是网关持服务账号 + 后端网关鉴权（设计文档 §5.1），避免 300s 码频繁过期。

## 安全不变量（后端 P0 已保证，网关无需重复）

- 绑定码 `type=im_bind`，仅放行 `/api/im/*`（路径白名单）
- `bound == ctx_user_id` 一致性校验：绑定码不可被未绑定他人借用
- 抢占防护：`im_user_id` 归属不可被他人绑定码改写（1004）
- 只读工具白名单：LLM 物理上无写工具，写操作回"请回 App 完成"

## 测试

```bash
./backend/tests/test_gateway.sh            # 起后端 + CLI 模拟: 未绑定引导/查账/写收敛/抢占防护
./backend/tests/test_im_bot_instance.sh    # iLink 用户级独立实例后端集成测试 (42 断言)
```

---

## 微信 iLink 用户级独立实例 (`ilink-server.js`)

每个 Minefolio 用户可在 App「设置 -> IM 机器人」创建 1 个独立的 iLink 微信机器人实例：

- `ilink-server.js`：supervisor 进程，每 5s 轮询 `GET /api/gateway/im/instances` 对账，按实例派生独立 `ilink/runner.js` 子进程。
- `ilink/state.js`：凭证 `account.json`、游标 `cursor.json`、去重 `dedup.json` 原子写入 `<ILINK_STATE_DIR>/<instance_id>/`（目录 `0700`、文件 `0600`），`bot_token` 永不上库；实例删除时自动清理子进程与目录。
- `ilink/login.js` / `ilink/monitor.js`：扫码、redirect、PIN 验证码、`-14` 过期停机、只响应 owner 私聊。

#### 微信对话记账（可选能力）

默认仍为只读查账。用户在 App「IM 机器人」中把实例能力切换为 `query+confirm` 后：

1. 微信里说「记一笔午餐 30 元」，机器人调用 `propose_daily_expense` 生成草案，回复摘要 + 8 位短码（如 `确认 A7K2M9PQ`）；
2. 用户回复「确认 A7K2M9PQ」，网关识别后调用 `POST /api/gateway/im/instances/:id/confirm` 落账（服务端验签 + 默认账本 editor 权限校验，不经过 LLM）；
3. 短码 5 分钟内一次性有效，重复回复会被拒绝。

注意：再次扫码登录不会重置能力开关；关闭能力后残留短码也将被拒绝。

### 启动 iLink 网关

```bash
cd gateway
export MINEFOLIO_API=http://localhost:8080
export MINEFOLIO_GATEWAY_TOKEN=<与后端一致的强随机 token>
export ILINK_STATE_DIR=./.ilink-state
npm run ilink
```

### 环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `MINEFOLIO_API` | `http://localhost:8080` | 后端地址 |
| `MINEFOLIO_GATEWAY_TOKEN` | **必填** | 与后端 `MINEFOLIO_GATEWAY_TOKEN` 一致，未设置立即退出 |
| `ILINK_STATE_DIR` | `gateway/.ilink-state` | 实例凭证与游标根目录（含敏感 `bot_token`，需妥善保管权限） |
| `ILINK_SUPERVISOR_POLL_MS` | `5000` | 对账间隔（ms） |
| `ILINK_HTTP_TIMEOUT_MS` | `45000` | iLink 长轮询请求超时（ms） |
| `ILINK_MAX_QUEUE` | `8` | 每实例串行消息队列上限 |
| `ILINK_REPLY_CHUNK` | `1800` | 长回复分片长度 |
| `ILINK_MAX_INSTANCES` | `20` | supervisor 允许的最大并发实例子进程数 |

### 网关单测

```bash
npm --prefix gateway test
```
