# Minefolio 安全审计报告

> 审计时间：2025-08-21  
> 审计范围：后端 C 服务（JWT / CSRF / 鉴权 / SQL）+ 前端 TypeScript  
> 结论：**2 个高危、5 个中危、4 个低危**，需优先处理

---

## 🔴 高危（High）

### H-1：JWT 密钥硬编码，可伪造任意用户 Token

**位置：** `backend/src/common/jwt.c:8`、`backend/src/middlewares/jwt_middleware.c:14`

```c
// jwt.c
static const char* jwt_secret(void) {
    const char* s = getenv("MINEFOLIO_JWT_SECRET");
    return s ? s : "minefolio-dev-secret-change-in-production";
}

// jwt_middleware.c（重复定义）
const char* secret = getenv("MINEFOLIO_JWT_SECRET");
if (!secret) secret = "minefolio-dev-secret-change-in-production";
```

**风险：** 任何知道该密钥的人可以用 HS256 算法伪造任意用户 ID 的合法 Token。攻击者只需：
```python
import hmac, hashlib, base64, json
secret = b"minefolio-dev-secret-change-in-production"
header = base64.urlsafe_b64encode(json.dumps({"alg":"HS256","typ":"JWT"}).encode()).rstrip(b'=').decode()
payload = base64.urlsafe_b64encode(json.dumps({"sub":1,"iat":1724000000}).encode()).rstrip(b'=').decode()
sig = base64.urlsafe_b64encode(hmac.new(secret, f"{header}.{payload}".encode(), hashlib.sha256).digest()).rstrip(b'=').decode()
token = f"{header}.{payload}.{sig}"
```
即可直接以 admin 身份访问所有 API（绕过 JWT 中间件）。

**修复：**
```c
static const char* jwt_secret(void) {
    const char* s = getenv("MINEFOLIO_JWT_SECRET");
    if (!s || s[0] == '\0') {
        fprintf(stderr, "ERROR: MINEFOLIO_JWT_SECRET must be set in production\n");
        exit(1);
    }
    return s;
}
```
同时删除 `jwt_middleware.c` 中的重复 fallback。

---

### H-2：CSRF Cookie 未设置 HttpOnly / Secure / SameSite 属性 ~~已修复~~

**位置：** `backend/src/middlewares/csrf_middleware.c:22`

```c
csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 0, 0);
//                                                  ↑path  ↑domain  ↑secure ↑httponly
//  secure=0, httponly=0 → Cookie 可通过 JS document.cookie 读取
```

**风险：**
1. **XSS 窃取 CSRF Token**：若站点存在任何 XSS 漏洞（如未来引入富文本输入），攻击者可读取 `document.cookie` 获取 CSRF Token，同时用于跨站请求。
2. **中间人窃取**：在 HTTP（非 HTTPS）环境下，Cookie 明文传输，可被网络嗅探。
3. **前端解析脆弱**：`http.ts` 用 `document.cookie.split('; ')` 手动解析，Cookie 格式异常时可能取不到 Token，导致 CSRF 防护形同虚设。

**修复：**
```c
// 需同时设置 secure=1, httponly=1，SameSite 通过额外 header 或框架支持设置
csilk_set_cookie(c, "csrf_token", buf, 86400, "/", NULL, 1, 1);
```
同时前端改为使用 `document.cookie` 的标准解析，或使用 `Cookie` 响应头直接读取。

---

## 🟠 中危（Medium）

### M-1：JWT 无过期时间（`exp` 字段缺失）

**位置：** `backend/src/common/jwt.c:14-15`

```c
csilk_json_add_int(payload, "sub", user_id);
csilk_json_add_int(payload, "iat", (int64_t)time(NULL));
// 缺少 csilk_json_add_int(payload, "exp", ...)
```

**风险：**
- Token 永久有效，直到服务端显式撤销（但当前无撤销机制）
- 用户修改密码后，旧 Token 仍合法（`auth_change_password` 不使旧 Token 失效）
- 设备丢失后无法通过"过期"自然终止访问

**修复：** 在 `jwt_generate_token` 中添加 `exp` 字段（建议 7 天）：
```c
csilk_json_add_int(payload, "exp", (int64_t)time(NULL) + 604800); // 7 days
```
并在 `jwt_middleware` 中验证 `exp` 是否过期。同时密码变更后清除所有旧 Token（需引入 token 黑名单或版本戳）。

---

### M-2：CORS 配置过于宽松（`*` + credentials）

**位置：** `backend/src/middlewares/cors_middleware.c:6-7`

```c
cors.allow_origin = "*";       // 允许任意来源
cors.allow_credentials = 1;    // 允许携带 Cookie/授权头
```

**风险：** 虽然 API 使用 Bearer Token 而非 Cookie 认证，但 `Access-Control-Allow-Credentials: true` + `Access-Control-Allow-Origin: *` 的组合违反 OWASP 安全建议。攻击者可以从任意恶意网站发起携带用户 JWT 的请求（如果用户同时访问了恶意站点）。

**修复：** 将 `allow_origin` 限制为实际前端域名列表，或至少在生产环境改为具体域名：
```c
const char* allowed = getenv("MINEFOLIO_CORS_ORIGIN");
cors.allow_origin = allowed ? allowed : "*";  // 生产环境强制设置环境变量
```

---

### M-3：无安全响应头

**位置：** 整个后端 `main.c` / 中间件层

未发现以下安全头部的设置：
- `Content-Security-Policy` — 防 XSS
- `X-Frame-Options: DENY` — 防点击劫持
- `X-Content-Type-Options: nosniff` — 防 MIME 嗅探
- `Strict-Transport-Security` — 强制 HTTPS
- `Referrer-Policy` — 控制 Referer 泄露

**风险：** 静态资源（Vue SPA）易受 XSS 和点击劫持攻击；HTTP 连接无升级保护。

**修复：** 在 `cors_middleware_wrapper` 或新增安全中间件中设置：
```c
csilk_set_header(c, "X-Frame-Options", "DENY");
csilk_set_header(c, "X-Content-Type-Options", "nosniff");
csilk_set_header(c, "Referrer-Policy", "strict-origin-when-cross-origin");
```

---

### M-4：注册/登录/初始化接口无速率限制

**位置：** `jwt_middleware.c` 豁免路径列表（`/api/auth/login`, `/api/auth/register`, `/api/system/setup`）

**风险：**
- `/api/auth/login`：可无限次尝试密码（无暴力破解防护）
- `/api/auth/register`：公开注册（仅系统未初始化时可用），仍可无限创建
- `/api/system/setup`：同上

**修复：** 在 JWT 中间件前添加简单 IP 级速率限制，或在各 handler 内实现计数限流（如 5 次/分钟/IP）。

---

### M-5：密码修改后旧 Token 仍有效

**位置：** `auth_change_password` (`auth_service.c:286`)

**风险：** 用户修改密码后，所有已分发的 JWT 仍然有效， attacker 持有旧 Token 仍可访问账户。

**修复：** 在密码变更时递增用户的 `token_version` 字段，JWT 中携带该版本，中间件校验版本号是否匹配。

---

### M-6：`get_last_insert_id()` 存在并发竞争窗口

**位置：** `backend/src/services/daily_expense_service.c:12`

```c
static int64_t get_last_insert_id(csilk_db_pool_t* pool) {
    csilk_json_t* res = csilk_db_query_json(pool, "SELECT last_insert_rowid() as id");
```

**风险：** `last_insert_rowid()` 是 SQLite 连接级概念。在 PostgreSQL 模式下（或通过连接池共享连接时），如果两个用户几乎同时插入，可能读到错误的 ID。虽然后续的 `WHERE user_id=?` 校验会过滤，但在事务内部、两次查询之间仍存在竞态。

**修复：** 改用 `RETURNING id` 模式（已在 `transactions_create` 中使用），或在 PostgreSQL 模式下使用 `currval(pg_get_serial_sequence(...))`。

---

## 🟡 低危（Low）

### L-1：`system_status` 暴露用户数量

**位置：** `backend/src/services/auth_service.c:33`

```c
csilk_json_add_number(resp, "user_count", count);
```

**风险：** 枚举攻击——攻击者可推断系统中有多少用户，辅助社会工程学或暴力破解优先级排序。

**修复：** 生产环境移除 `user_count`，或返回固定值 `1`。

---

### L-2：SQL 拼接模式（`tag_ids` IN 子句）

**位置：** `backend/src/services/daily_expense_service.c:128-131`

```c
snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql),
    " AND EXISTS (SELECT 1 FROM expense_tags et2 "
    " WHERE et2.expense_id=de.id AND et2.tag_id IN (%s))", tag_ids);
```

**现状：** 参数先经字符白名单校验（只允许数字和逗号），再拼入 SQL。由于 `tag_id` 是整数列，且输入被限制为纯数字+逗号，**当前不可利用**。

**风险：** 模式危险，未来修改时如果放宽校验可能引入真正注入。

**修复：** 将逗号分隔字符串拆分为数组后逐条参数绑定：
```c
// 将 tag_ids="1,2,3" → params = {uid, "1"}, {uid, "2"}, {uid, "3"}
```

---

### L-3：前端 CSRF Token 手动解析 Cookie

**位置：** `frontend/src/utils/http.ts:30`

```typescript
const csrf = document.cookie.split('; ').find((r) => r.startsWith('csrf_token='))?.split('=')[1]
```

**风险：** `document.cookie` 格式依赖服务端 `Set-Cookie` 的精确输出。如果 csilk 框架后续添加属性（如 `; Secure; SameSite=Lax`），split 逻辑可能失效，导致 CSRF 头缺失，触发 403 或静默失效。

**修复：** 使用标准解析工具：
```typescript
function getCookie(name: string): string | null {
  const match = document.cookie.match(new RegExp('(?:^|; )' + name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '=([^;]*)'))
  return match ? decodeURIComponent(match[1]) : null
}
```

---

### L-4：`/api/auth/public-key` 无需鉴权但返回 RSA 公钥信息

**位置：** `jwt_middleware.c:11`（已豁免）

**风险：** 公钥本身可公开，无安全风险。但结合 H-1 的硬编码密钥问题，了解公钥结构后配合已知 JWT secret 可完整伪造请求。

---

## 📊 风险总览

| 级别 | 数量 | 关键项 |
|------|------|--------|
| 🔴 高危 | 1 | JWT 硬编码密钥、CSRF Cookie 无安全标志 |
| 🟠 中危 | 4 | JWT 无过期、CORS 过宽、缺安全头、无速率限制、密码变更不失效旧 Token |
| 🟡 低危 | 4 | 用户数泄露、SQL 拼接模式、前端 Cookie 解析脆弱、公钥暴露 |

## 🔧 优先修复顺序

1. **H-1** — JWT 密钥强制环境变量（10 分钟）
2. **H-2** — CSRF Cookie 加 HttpOnly+Secure 标志（5 分钟）
3. **M-1** — JWT 加 `exp` 过期字段（10 分钟）
4. **M-3** — 添加安全响应头中间件（15 分钟）
5. **M-4** — 登录/注册接口添加简单速率限制（30 分钟）
6. **M-5** — 密码变更后递增 token_version（20 分钟）
7. **L-2/L-3** — 代码重构消除拼接模式（30 分钟）
