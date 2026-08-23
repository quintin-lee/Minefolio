# Minefolio 服务器段错误崩溃诊断报告

## 一、问题概述

**现象**: 请求 `GET /api/auth/public-key` 时服务器发生段错误 (SIGSEGV) 崩溃

**影响范围**: 
- 仅影响 `/api/auth/public-key` 端点
- 其他端点 (`/healthz`, `/api/system/status`) 正常工作
- 服务器进程崩溃，需要重启

**复现概率**: 100%（每次请求该端点都会崩溃）

---

## 二、环境信息

| 项目 | 版本 |
|------|------|
| 操作系统 | Linux manjaro 6.12.101-1-MANJARO x86_64 |
| 编译器 | GCC 16.1.1 |
| csilk 框架 | v0.5.2 (commit a56a4e6) |
| OpenSSL | 3.6.3 |
| SQLite | 3.53.4 |
| 构建类型 | Release |

---

## 三、崩溃触发路径

### 3.1 请求流程

```
HTTP GET /api/auth/public-key
    ↓
JWT 中间件 (jwt_middleware.c:48-52)
    ├── 匹配豁免路径 ✓
    ├── 调用 csilk_next(c) ✓
    └── return
    ↓
路由处理器 (auth_public_key)
    ├── 创建静态 JWK 字符串
    └── 调用 csilk_json_string(c, 200, jwk)
    ↓
[CRASH] Segmentation fault
```

### 3.2 关键代码

**jwt_middleware.c:48-52** (已修复)
```c
if (path && (strcmp(path, "/api/auth/login") == 0 ||
             strcmp(path, "/api/auth/register") == 0 ||
             strcmp(path, "/api/system/status") == 0 ||
             strcmp(path, "/api/system/setup") == 0 ||
             strcmp(path, "/api/auth/public-key") == 0)) {
    csilk_next(c);  // 添加此行后仍崩溃
    return;
}
```

**key_manager.c:32-36** (当前简化版本)
```c
void auth_public_key(csilk_ctx_t* c) {
    const char* jwk = "{\"kty\":\"RSA\",\"n\":\"test\",\"e\":\"AQAB\"}";
    csilk_json_string(c, 200, jwk);  // 崩溃点
}
```

**csilk_json_string 实现** (response.c)
```c
void csilk_json_string(csilk_ctx_t* c, int status, const char* json_str) {
    if (!c || !json_str) {
        return;
    }
    c->response.status = status;
    csilk_set_header(c, "Content-Type", "application/json");  // 可能崩溃点1
    csilk_response_body_release(c);  // 可能崩溃点2
    c->response.body = json_str;
    c->response.body_len = strlen(json_str);
    c->response.body_ownership = CSILK_OWN_BORROWED;
}
```

---

## 四、已尝试的修复方案

### 方案 1: 简化处理器 (使用静态 JWK)
```c
const char* jwk = "{\"kty\":\"RSA\",\"n\":\"test\",\"e\":\"AQAB\"}";
csilk_json_string(c, 200, jwk);
```
**结果**: ❌ 仍然崩溃

### 方案 2: 使用 respond_ok 宏
```c
csilk_json_t* resp = csilk_json_object();
csilk_json_add_string(resp, "public_key", jwk);
respond_ok(c, resp);
```
**结果**: ❌ 仍然崩溃

### 方案 3: 自定义 base64url 编码
```c
static void base64url_encode(const uint8_t* src, size_t len, char* dst) { ... }
csilk_json_string(c, 200, jwk);
```
**结果**: ❌ 仍然崩溃

### 方案 4: 不使用 csilk_json_string
```c
// 直接设置响应
c->response.status = 200;
csilk_set_header(c, "Content-Type", "application/json");
c->response.body = jwk;
c->response.body_len = strlen(jwk);
c->response.body_ownership = CSILK_OWN_BORROWED;
```
**结果**: ❌ 仍然崩溃

---

## 五、崩溃特征分析

### 5.1 崩溃时机
- 服务器启动正常
- `/healthz` 请求正常
- `/api/system/status` 请求正常
- **仅** `/api/auth/public-key` 请求崩溃

### 5.2 日志输出
```
2026-08-23 18:xx:xx INFO  [http1_parse.c:415] _csilk_dispatch_request(): Request: GET /api/auth/public-key
2026-08-23 18:xx:xx INFO  [logger.c:116] csilk_logger_handler(): <xxx> [HTTP] GET /api/auth/public-key 200 0.000xxx s
2026-08-23 18:xx:xx INFO  [http1_pipeline.c:53] _csilk_handle_post_response(): <xxx> _csilk_handle_post_response called, keep_alive=1
Segmentation fault (core dumped)
```

### 5.3 关键观察
1. 日志显示请求处理完成 (200 OK)
2. 但在 `_csilk_handle_post_response` 调用后崩溃
3. 崩溃发生在 response pipeline cleanup 阶段

---

## 六、可能的根本原因

### 原因 1: handler_chain 状态污染
- JWT 中间件虽然调用了 `csilk_next(c)`，但可能有残留状态
- `handler_index` 或 `handlers` 指针可能未正确初始化/清理
- middleware chain 执行后状态不一致

### 原因 2: response body 释放问题
- `csilk_response_body_release(c)` 可能释放了无效内存
- 可能与 arena allocator 生命周期有关
- 第一次请求后 arena 状态异常

### 原因 3: OpenSSL 初始化问题
- `auth_key_init()` 调用 `_csilk_generate_keypair(NULL, ...)`
- 传递 NULL context 可能导致 OpenSSL 全局状态问题
- OpenSSL 3.x 的 provider 机制可能受影响

### 原因 4: csilk v0.5.2 框架 bug
- 可能是 csilk 框架本身的 bug
- 与特定 middleware + handler 组合相关
- 需要框架开发者深入调试

---

## 七、调试建议

### 7.1 使用 GDB 获取堆栈跟踪
```bash
gdb ./build/minefolio
set env MINEFOLIO_DB_DRIVER=sqlite
set env MINEFOLIO_DB_DSN=/tmp/test.db
handle SIGSEGV stop noprint pass
run
continue
bt full
```

### 7.2 使用 Valgrind 检测内存错误
```bash
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
         --track-origins=yes ./build/minefolio
```

### 7.3 使用 ASAN 检测内存问题
```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build_asan
ASAN_OPTIONS=detect_leaks=1 ./build_asan/minefolio
```

### 7.4 最小化复现测试
创建独立测试程序，逐步排除 minefolio 特定代码：
```c
// 测试 1: 直接使用 csilk_json_string
// 测试 2: 通过 JWT 中间件后调用
// 测试 3: 模拟完整请求流程
```

---

## 八、临时解决方案

### 方案 A: 绕过 JWT 中间件
```c
// 在 main.c 中直接注册，不经过 /api 组
csilk_app_get(app, "/api/auth/public-key", auth_public_key);
```

### 方案 B: 使用 nginx 代理
```nginx
location = /api/auth/public-key {
    default_type application/json;
    return 200 '{"kty":"RSA","n":"...","e":"..."}';
}
```

### 方案 C: 降级 csilk
临时使用 csilk v0.4.x 版本，直到 v0.5.2 问题修复。

---

## 九、复现步骤

```bash
# 1. 克隆项目
git clone https://github.com/quintin-lee/Minefolio.git
cd Minefolio/backend

# 2. 构建
rm -rf build
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# 3. 启动服务器
MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN=/tmp/test.db ./build/minefolio &

# 4. 测试正常端点
curl http://127.0.0.1:8080/healthz
curl http://127.0.0.1:8080/api/system/status

# 5. 触发崩溃
curl http://127.0.0.1:8080/api/auth/public-key
# 输出: Segmentation fault (core dumped)
```

---

## 十、联系信息

- **Minefolio 项目**: https://github.com/quintin-lee/Minefolio
- **csilk 框架**: https://github.com/quintin-lee/csilk
- **问题报告**: 请附上完整堆栈跟踪和复现步骤
