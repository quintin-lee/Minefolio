# Minefolio IM 网关 (gateway/)

桥接 QQ / 微信消息到后端只读查账端点 `/api/im/*`。零外部依赖，仅用 Node ≥ 18 内置 `fetch`。

## 文件

| 文件 | 职责 |
|------|------|
| `index.js` | 网关核心：`handleImMessage(platform, imUserId, text, bindToken)` + `/bind` 处理 + `refreshBindToken` |
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

P1 的绑定码是**静态配置**（`ONEBOT_BIND_TOKEN`/`ONEBOT_BIND_MAP`）。P2-A 会用后端
`/im/{platform}/bind-auth` 替换 `getBindToken`，让 IM 用户自助绑定。

### 测试（不连真 IM）

```bash
cd gateway
npm run test:adapters      # node --test adapters/test/  (5 用例: 桥接/群忽略/wechaty guard)
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
./backend/tests/test_gateway.sh     # 起后端 + CLI 模拟: 未绑定引导/查账/写收敛/抢占防护 (4 断言)
```
