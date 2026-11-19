# 接入操作手册 — QQ / 微信机器人只读查账

> 后端 P0（`/api/im/*` 5 端点 + V027 + 只读工具白名单 + 抢占防护）已就绪。
> 本手册覆盖「网关 + App 绑定 UI + QQ/微信 框架」三层剩余工作的接入步骤，暂不含代码实现。

## 0. 依赖关系

```
QQ/微信框架 (NapCat / wechaty)          [P1+ 需自备]
        │  收发消息
        ▼
网关 (Node, gateway/)                   [需实现]
        │  调 REST
        ▼
后端 /api/im/*  (P0 ✅ 已就绪)
        ▲
        │  App 生成绑定码
App「IM 机器人」卡片                    [需实现 P3]
```

**接入顺序**：① 起后端 → ② 实现并起网关 → ③ App 生成绑定码 → ④ 在 QQ/微信里 `/bind <码>` → ⑤ 发消息查账。

## 1. 起后端（已就绪，仅验证）

```bash
# 配好 AI 模型 (ai_config / MINEFOLIO_AI_* 或 admin 设置页), 只读查账 LLM 必须可用
cd backend
./build/minefolio            # 或 MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=... ./build/minefolio
```

验证端点注册成功：
```bash
curl -s http://localhost:8080/api/im/bindings -H "Authorization: Bearer <你的App登录token>"
# 期望: HTTP 200 + {"code":0,"data":[]}  (未绑定时空数组)
```

## 2. 实现网关（Node, 需新建 gateway/）

网关是唯一未实现、且**必须**的桥接件。职责：

| 功能 | 调用后端 | 说明 |
|------|---------|------|
| 收消息查账 | `POST /api/im/{platform}/chat` body `{im_user_id, message}` | 带 `im_bind` 绑定码做 Bearer；回 `data.reply` |
| 收 `/bind <码>` | `POST /api/im/{platform}/bind` body `{im_user_id}` | 绑定码作 Bearer；回绑定成功 |
| 会话映射 | 本地 `session_map: {platform:im_user_id → session_key}` | 可省略（后端已按固定标题映射） |

**最小网关伪代码**（实现时参考）：
```js
async function handleImMessage(platform, imUserId, text, bindToken) {
  if (text.startsWith('/bind ')) {
    const code = text.slice(6).trim();
    await fetch(`${API}/api/im/${platform}/bind`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${code}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ im_user_id: imUserId }),
    });
    return '绑定成功，现在可以问我持仓/汇率了';
  }
  const res = await fetch(`${API}/api/im/${platform}/chat`, {
    method: 'POST',
    headers: { Authorization: `Bearer ${bindToken}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ im_user_id: imUserId, message: text }),
  });
  const json = await res.json();
  return json.data?.reply ?? 'AI 查询失败';
}
```

> 注意：网关持有的 `bindToken` 是某个 Minefolio 用户的 `im_bind` 绑定码（300s，需 App 刷新）。
> 生产建议：网关按 `platform:im_user_id` 缓存各用户绑定码，定期刷新；或改为网关服务账号 + 后端网关鉴权。

## 3. App「IM 机器人」卡片（P3，需实现前端）

位置：`frontend/src/views/Settings.vue` 新增卡片（复用 `AccountDialog` 模式），功能：
1. 选平台（QQ / 微信）
2. 显示 IM 账号标识（QQ 号 / 微信号，由用户填）
3. 「生成绑定码」→ 调 `POST /api/im/bindings/token` → 展示 300s 的绑定码
4. 用户把绑定码发给 QQ/微信 机器人（机器人自动 `/bind`）
5. 「已绑定列表 / 解绑」→ `GET/DELETE /api/im/bindings`

对应前端 API：`frontend/src/api/im.ts`（新增，封装 5 端点，走 `http.ts` 中心 client）。

## 4. QQ / 微信 框架接入（需自备运行环境）

### 4.1 QQ（推荐 NapCat，OneBot v11 协议）
```
1. 部署 NapCat (Docker 或独立进程), 登录你的 QQ 号 (机器人号)
   - 注意: 用非主号, 遵守 QQ 风控; 自建号更安全
2. NapCat 暴露 OneBot v11 HTTP/WS (默认 ws://localhost:3001)
3. 网关订阅 OneBot 事件:
   - message 事件 (C2C/群) → 取 im_user_id = sender.id → handleImMessage('qq', ...)
   - 私聊 (C2C) 优先, 群聊 @机器人 再处理
4. 网关回消息: 调 NapCat 的 send-private-msg API
```
最小命令：
```bash
docker run -d --name napcat -p 3001:3001 mlikiowa/napcat:latest   # 按官方仓库为准
```

### 4.2 微信个人号（wechaty, 需 puppet license）
```
1. npm i wechaty puppet-wechat4u (或 XWechat 等 puppet; 部分需 license)
2. 写 wechaty bot:
   - bot.on('message', m => handleImMessage('wechat', m.from(), m.text()))
   - bot.say(reply)
3. 网关复用 handleImMessage, platform='wechat'
   注意: wechaty 个人号有封号风险, 建议测试号
```

## 5. 启动顺序（完整链路）

```bash
# 1. 后端
./backend/build/minefolio &
# 2. QQ 框架 (二选一)
docker start napcat          # QQ
# 或 node gateway/wechat-bot.js   # 微信 wechaty
# 3. 网关 (连后端 + 连 QQ/微信 框架)
node gateway/index.js        # API=http://localhost:8080 API_TOKEN=...
# 4. App: 生成绑定码 → 发给机器人 → /bind 完成 → 发"我最近一个月花了多少"
```

## 6. 验证闭环

| 步骤 | 期望 |
|------|------|
| App 生成绑定码 | 拿到 300s 的 `bind_token` |
| 机器人 `/bind <码>` | 回"绑定成功" |
| 机器人 "我最近一个月花了多少" | 回 AI 查询结果（金额，CNY，含 emoji 分级） |
| 机器人 "记一笔 200 吃饭" | 回"请回到 App 完成写操作"（只读收敛生效） |
| 用他人绑定码查 | 被拒（抢占防护 + 隔离） |

## 7. 安全与运维要点

- 绑定码 300s 短 TTL，路径白名单（仅 `/api/im/*`）
- 抢占防护：`im_user_id` 归属不可被他人绑定码改写（1004）
- 网关持有绑定码需刷新机制；生产建议网关服务账号替代 300s 码
- 个人号（微信）封号风险：测试号起步；QQ 用自建机器人号
- 多实例部署：汇率同步单实例假设仍适用（与 IM 接入无关，但同机部署需知）

## 8. 待实现项清单（按优先级）

- [ ] P1 网关核心 `gateway/`（Node，含 OneBot/wechaty 适配器）
- [ ] P1 网关绑定码刷新（按 im_user_id 缓存 + 周期刷新 / 服务账号）
- [ ] P3 App「IM 机器人」卡片 + `frontend/src/api/im.ts`
- [ ] 运维：Docker 多服务编排（minefolio + napcat + gateway）
