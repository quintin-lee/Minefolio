# AI 配置生命周期下沉与服务单体瘦身设计规范 (Design Specification)

## 1. 意图与背景 (Intent & Why)

当前 `backend/src/services/ai_service.c` 已膨胀至近 1300 行（单体服务），并在内部揉合了三大职责：
1. **全局配置快照管理**：`ai_config_snapshot_t` 结构体、RCU 引用计数、`pthread_mutex` 锁、预建 `csilk_ai_t` 实例以及 `ai_init` / `ai_shutdown` 生命周期；
2. **外部网络探测与元数据发现**：通过 libcurl 和临时 `csilk_ai_t` 发起连通性测试（`ai_service_test_connection`）及动态模型发现（`ai_service_fetch_models`）；
3. **SSE 流式传输与运行时桥接**：SSE 客户端事件封装（`sse_bridge_*`）与聊天入口（`ai_chat_handler`）。

**核心架构隐患与痛点：**
- **UAF 与引用泄漏根源**：配置快照本应受到严格的 `ai_config_acquire()` / `ai_config_release()` 引用计数保护，但历史保留的 `ai_get_config()` 别名掩盖了获取引用的语义，导致调用方误以为只是取裸指针而漏调 `release()`。经审计，`application/ai/usecases.c:53`（`ai_usecase_models_list`）每次调用都会永久泄漏一个快照引用；外部控制器或工作流稍有不慎更会导致野指针悬空（如热重载释放底层配置后的 UAF）。
- **严重违反 DDD 分层**：`application/ai/usecases.c` 在更新配置时，为了触发重载，使用了 `extern void ai_init(csilk_db_pool_t* pool); extern void ai_shutdown(void);` 反向穿透调用服务层；而控制器的连通性测试与模型拉取直接越过应用层调用服务层。
- **单体服务维护成本高**：`ai_service.c` 承担过多正交职责，代码量接近 1300 行，难以实施轻量化单元测试。

本设计旨在将 AI 配置快照生命周期下沉到 `application/ai/` 层，收拢并废除 `ai_get_config()`，同时解耦外部探测用例，使 `ai_service.c` 缩减为纯粹的 SSE 消息桥，彻底消除 UAF 与引用泄漏隐患。

---

## 2. 目标、非目标与反向目标 (Goals, Non-Goals & Anti-Goals)

### 目标 (Goals)
1. **生命周期下沉**：在 `application/ai/` 增加 `config_provider.h/.c`，完整接管 `ai_config_snapshot_t` 的存储、原子引用计数、互斥锁、预建实例与 RCU 热重载。
2. **彻底消灭隐式接口与泄漏**：废除全局 `ai_get_config()`，全工程调用点统一迁移至受守卫的 `ai_config_acquire()` / `ai_config_release()` 或 RAII 宏 `AI_CONFIG_SCOPE`；修复 `ai_usecase_models_list` 的引用泄漏。
3. **消除 DDD 反向依赖**：移除 `application/ai/usecases.c` 中的局部 `extern void ai_init/shutdown` 声明，统一由应用层内部调用 `ai_config_provider_reload(pool)`。
4. **服务层解耦瘦身**：将 libcurl 驱动的 `test_connection` 与 `fetch_models` 抽离至 `application/ai/probe_usecase.h/.c`，控制器通过应用层用例调用；`services/ai_service.c` 仅保留 SSE 桥接与聊天入口，代码量压缩至 600 行以内。
5. **严密测试验证**：提供专用的配置提供者单元测试 `tests/unit/test_ai_config_provider.c`，确保并发 acquire/release、热重载替换与归零销毁无任何竞态或内存泄漏。

### 非目标 (Non-Goals)
- 不改变前端与后端的 HTTP API 协议定义（`/api/settings/ai/test`、`/api/settings/ai/fetch-models` 等接口签名与 JSON 返回体 100% 保持不变）。
- 不重写 Unified AI Runtime（`services/ai/runtime/`）的执行循环与工具分发协议。
- 不引入复杂的外部依赖（如 libuv/libevent 等），继续遵循 csilk 与 C23 标准线程库。

### 反向目标 (Anti-Goals)
- **禁止保留任何静默无引用的全局配置读取接口**：不得为了“向后兼容”保留无需 release 的 `ai_get_config()`。
- **禁止在用例层破坏 `void* pool` 契约**：用例层对数据库连接池的抽象必须继续保持 `void* pool`。
- **禁止让重载阻塞正在处理的流式会话**：热重载必须保持 RCU 特性，不能因为更新配置而打断已在进行中的长耗时请求。

---

## 3. 架构设计与分层依赖 (Architecture & Layering)

### 3.1 重构前后依赖流向对比

```
【重构前 (存在反向穿透与单体膨胀)】:
main.c
  └──> services/ai_service.c (持有 g_snapshot, 1299 行单体)
         ├── RCU Snapshot & ai_get_config()
         ├── libcurl test_connection & fetch_models
         ├── ai_summary_pool & mcp_pool 初始化
         └── SSE Bridge & Chat Handler
controllers/ai_controller.c
  ├──> services/ai_service.c (直接调用 test_connection / fetch_models)
  └──> application/ai/usecases.c
         ├──> ai_get_config() [泄漏引用]
         └──> [extern 逆向] services/ai_service.c: ai_shutdown() / ai_init() [副作用：杀掉MCP池重置线程]

【重构后 (纯净单向 DDD 依赖与生命周期解耦)】:
main.c
  └──> services/ai_service.h (ai_init / ai_shutdown 服务级协调入口)
controllers/ai_controller.c
  ├──> application/ai/usecases.h
  ├──> application/ai/probe_usecase.h (test_connection / fetch_models)
  └──> services/ai_service.h (仅保留 ai_chat_handler / stream_report)
application/ai/
  ├── config_provider.h/.c (纯净应用层：管理快照、原子计数、RCU reload、acquire/release)
  ├── probe_usecase.h/.c   (外部连通性与模型探测)
  └── usecases.c           (使用 config_provider_reload，无反向 extern，不波及工作线程/MCP)
services/ai_service.c (约 500-600 行纯粹的 SSE 消息桥接与服务协调)
  ├── 委托 application/ai/config_provider 管理配置生命周期
  ├── 协调 ai_summary_pool / mcp_pool 启动与停止
  └── 读取 application/ai/config_provider 进行聊天流式驱动
```

---

## 4. 方案决策与对比 (Approaches & Tradeoffs)

在方案探讨中评估了以下三种途径：

| 方案 | 核心做法 | 优势 | 劣势 | 结论 |
|------|---------|------|------|------|
| **方案 A：轻量收拢** | 仅剥离快照生命周期至 `config_provider.c`，收敛 acquire/release 并废除 `ai_get_config()` | 改动面小，风险极低 | `ai_service.c` 仍有 1000+ 行，curl 探测与网络元数据依然污染服务层 | 未彻底消除膨胀隐患 |
| **方案 B：深度瘦身 (已选定)** | 下沉 `config_provider` + 剥离外部探测至 `probe_usecase` + 废除 `ai_get_config` | 一举根治 UAF/引用泄漏，消除反向 extern 依赖，`ai_service.c` 体积减半，职责清晰 | 需调整控制器中 2 个接口的用例调用 | **最佳方案，彻底解决问题** |
| **方案 C：请求上下文强绑定** | 完全禁止全局 acquire，所有配置由 `ai_runtime_context_t` 在请求入口注入 | 编译期/框架级杜绝手动 release 遗漏 | 票据 OCR、后台异步摘要、设置等模块无 Context，侵入过深，破坏稳定性 | 成本过高，暂不采纳 |

---

## 5. 详细实现规格 (Detailed Implementation Specification)

### 5.1 配置提供者 `application/ai/config_provider.h/.c`

```c
#pragma once

#include "common/ai_config.h"
#include "csilk/csilk.h"
#include "csilk/drivers/ai.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AI 配置提供者（应用启动或集成测试启动时调用）
 */
void ai_config_provider_init(void* pool);

/**
 * @brief 关闭 AI 配置提供者并释放最后写者持有的快照引用
 */
void ai_config_provider_shutdown(void);

/**
 * @brief 热重载 AI 配置（settings 更新后由用例层主动调用，原子替换快照）
 * @return 0 成功，非 0 失败
 */
int ai_config_provider_reload(void* pool);

/**
 * @brief 获取当前配置快照引用（原子 refcount++）
 *        调用方在用完后必须显式调用 ai_config_release()！
 * @return ai_config_t* 配置快照指针，未就绪时返回 NULL
 */
ai_config_t* ai_config_acquire(void);

/**
 * @brief 释放持有的配置快照引用（原子 refcount--，归零时物理释放内存）
 * @param cfg 由 ai_config_acquire() 获取的配置指针
 */
void ai_config_release(ai_config_t* cfg);

/**
 * @brief 获取快照内预建的 csilk_ai_t 实例（须在持有 cfg 保护下调用）
 */
csilk_ai_t* ai_config_get_ai_instance(const ai_config_t* cfg);

/**
 * @brief RAII 风格作用域守卫宏（退出代码块时自动执行 release）
 */
#define AI_CONFIG_SCOPE(cfg)                                             \
    for (ai_config_t *cfg = ai_config_acquire(), *__once = (void*)1;    \
         __once && cfg;                                                  \
         __once = NULL, ai_config_release(cfg))

#ifdef __cplusplus
}
#endif
```

#### 快照生命周期状态机与内存模型
- `ai_config_snapshot_t` 结构体私有化在 `config_provider.c` 中，外部仅能看到不透明的 `ai_config_t*`。
- 写者线程（`init` / `reload`）：深拷贝 DB/JSON 配置，初始化预建 `csilk_ai_t` 驱动，设置写者 `refcount = 1`。在持 `pthread_mutex_t` 保护下原子交换 `g_snapshot` 指针。释放旧快照写者引用（`snapshot_writer_release`）。
- 读者线程（各请求处理）：`ai_config_acquire()` 在锁内读取 `g_snapshot` 并执行 `atomic_fetch_add(&s->refcount, 1)`。读者处理期间无论发生多少次配置热重载，旧快照内存绝不会被提前释放。
- 读者释放：`ai_config_release()` 执行 `atomic_fetch_sub(&s->refcount, 1)`，若降为 0 则通过 `ai_config_free(&s->config)` 和 `csilk_ai_free(s->ai_instance)` 回收所有资源。

### 5.2 彻底移除 `ai_get_config()` 并修复泄漏点

审计并修正以下所有 7 处调用点：
1. `backend/src/application/ai/usecases.c:53` (`ai_usecase_models_list`):
   - 改为 `ai_config_acquire()`，并在序列化完成后（或错误退出分支）调用 `ai_config_release(cfg)`，**彻底修复每次查询泄漏 1 个引用的 Bug**。
2. `backend/src/application/ai/usecases.c:317` (`ai_usecase_settings_get`):
   - 由 `ai_get_config()` 改为 `ai_config_acquire()`，保留末尾配对的 `ai_config_release(cfg)`。
3. `backend/src/interfaces/http/controllers/receipt_controller.c:299`:
   - 由 `ai_get_config()` 改为 `ai_config_acquire()`，配对原有的 `ai_config_release(cfg)`。
4. `backend/src/services/ai/memory/summary.c:485`:
   - 由 `ai_get_config()` 改为 `ai_config_acquire()`，配对原有的 `ai_config_release(cfg)`。
5. `backend/src/services/ai/runtime/loop.c:1335`:
   - 由 `ai_get_config()` 改为 `ai_config_acquire()`，配对原有的 `ai_config_release(cfg)`。
6. `backend/src/services/ai/runtime/runtime.c:21`:
   - 移除弃用的 `ai_runtime_get_config()`，或内部改为 `ai_config_acquire()`。
7. `backend/tests/unit/test_ai_summary.c:157`:
   - 改为 `ai_config_acquire()`，配对原有的 `ai_config_release(cfg)`。
8. 删去 `services/ai_service.h` 和 `services/ai_service.c` 中 `ai_get_config()` 的函数声明与实现，彻底切断该接口。

### 5.3 修复 `usecases.c` 局部 `extern` 反向依赖

在 `backend/src/application/ai/usecases.c` 中：
```c
// 移除原有的:
// extern void ai_init(csilk_db_pool_t * pool);
// extern void ai_shutdown(void);
// ai_shutdown();
// ai_init(db_pool);

// 改为纯净正向调用:
#include "application/ai/config_provider.h"
...
ai_config_provider_reload(db_pool);
```

### 5.4 剥离外部探测与模型发现至 `application/ai/probe_usecase.h/.c`

在 `application/ai/` 引入独立的用例定义：
```c
// application/ai/probe_usecase.h
#pragma once

#include "csilk/csilk.h"
#include "application/ai/dtos.h"

int ai_usecase_test_connection(const csilk_json_t* body, csilk_json_t** out_resp, ai_usecase_result_t* out_res);
int ai_usecase_fetch_models(const csilk_json_t* body, csilk_json_t** out_resp, ai_usecase_result_t* out_res);
```
- 将 `ai_service.c` 中的 `ai_service_test_connection` 核心逻辑（密钥解密、驱动创建、ping 消息测试、友好错误格式化）搬迁至 `ai_usecase_test_connection`。
- 将 `ai_service.c` 中的 `ai_service_fetch_models` 核心逻辑（CURL 回调、Ollama / OpenAI `/models` 接口请求与 JSON 解析）搬迁至 `ai_usecase_fetch_models`。
- 控制器 `interfaces/http/controllers/ai_controller.c`：
  - `api_ai_test_connection_handler`：解析请求体，调用 `ai_usecase_test_connection`，通过 `respond_ok(c, out_resp)` 输出。
  - `api_ai_fetch_models_handler`：解析请求体，调用 `ai_usecase_fetch_models`，通过 `respond_ok(c, out_resp)` 输出。
- 从 `services/ai_service.c` 与 `services/ai_service.h` 中彻底剔除这两个函数及 `curl_write_memory_cb`。

### 5.5 `services/ai_service.c` 职责收敛与瘦身

- **剥离配置快照存储与 RCU 管理**：
  - 移除私有 `ai_config_snapshot_t` 结构体、`g_snapshot`、`g_snapshot_lock`、`make_snapshot`、`snapshot_writer_release`；
  - 移除 `ai_config_acquire`、`ai_config_release`、`ai_snapshot_ai_instance` 的实现（下沉至 `application/ai/config_provider.c`）；
  - 彻底删除 `ai_get_config()`。
- **剥离外部探测与模型拉取**：
  - 移除 `ai_service_test_connection`、`ai_service_fetch_models`、`curl_write_memory_cb`（迁移至 `application/ai/probe_usecase.c`）。
- **保留服务级生命周期协调器 (`ai_init` / `ai_shutdown`)**：
  - `ai_init(pool)`：负责协调子系统初始化，依次调用 `ai_config_provider_init(pool)` 与 `ai_summary_pool_init()`；
  - `ai_shutdown()`：负责协调子系统关闭，依次调用 `ai_summary_pool_shutdown()`、`mf_mcp_stdio_pool_shutdown()` 与 `ai_config_provider_shutdown()`；
  - 这样既避免了在应用层 `config_provider` 中跨层反向依赖服务层进程池/线程池（严格保持 DDD 分层），又确保 `main.c` 和现有测试套件无需变动生命周期调用。
- **保留核心流式桥接**：
  - 保留 SSE 桥接回调与文本/思考/工具分块推送（`sse_bridge_*`）；
  - 保留聊天主入口（`ai_chat_handler`）与报告流式入口（`ai_service_stream_report`）；内部直接通过 `ai_config_acquire()` / `ai_config_release()` 读取快照；
- **体积瘦身成果**：代码行数从 **1299 行** 缩减至 **550 行以内**，降幅超 55%。

### 5.6 系统启动、关闭与热重载的职责划分

1. **服务启动 (`main.c` / 集成测试)**：
   - 调用 `ai_init(pool)`，统一拉起配置提供者 `ai_config_provider_init(pool)` 与后台工作线程池 `ai_summary_pool_init()`。
2. **服务停止 (`main.c` / 集成测试)**：
   - 调用 `ai_shutdown()`，安全关闭后台工作线程 `ai_summary_pool_shutdown()`、终止 MCP 子进程 `mf_mcp_stdio_pool_shutdown()`，最后注销快照提供者 `ai_config_provider_shutdown()`。
3. **配置热重载 (`application/ai/usecases.c:ai_usecase_settings_update`)**：
   - 仅调用 `ai_config_provider_reload(db_pool)` 进行配置快照的无缝原子替换；
   - **不再调用 `ai_shutdown()` / `ai_init()`**，彻底根治此前每次修改设置都会无故销毁 MCP 子进程池与重建工作线程池的历史副作用。

---

## 6. 验证与测试计划 (Verification Plan)

### 6.1 单元测试 (`backend/tests/unit/test_ai_config_provider.c`)
新增专门的单元测试用例，覆盖：
1. **多线程并发 Acquire/Release 压力测试**：启动 8 个读者线程持续 acquire 和 release，验证无死锁、无悬空指针、引用计数精确归零；
2. **并发热重载（RCU 验证）**：在读者持有旧快照期间触发 `ai_config_provider_reload`，验证写者成功交换新快照，且持有旧快照的读者指针数据完好，直到所有读者 release 后旧快照才被释放；
3. **关闭与销毁**：验证 `ai_config_provider_shutdown()` 后再 acquire 安全返回 NULL，无内存泄漏（Valgrind / ASan 清白）。

### 6.2 现有集成测试验证
1. `tests/unit/test_ai_summary.c`：全绿通过；
2. `tests/unit/test_ai_runtime.c`：全绿通过；
3. `tests/unit/test_ai_policy.c`：全绿通过；
4. 全量集成测试 `./tests/test_link.sh`（40 个用例，174 个断言）：全绿通过；
5. 全量编译验证：`cmake --build backend/build --parallel` 0 警告、0 错误。

---

## 7. 预留给实现者的自由裁量点 (Left to the Builder)
1. **宏命名与便利函数**：除 `AI_CONFIG_SCOPE` 外，是否需要在 `config_provider.h` 增加 `ai_config_has_configured()` 等微型只读辅助函数由实现者自行决定。
2. **友好错误提示共享**：`format_friendly_error` 既被聊天 SSE 错误处理使用，也被连通性测试使用，可下沉至 `application/ai/probe_usecase.c` 或通用头文件共享。
3. **兼容过渡别名**：`ai_init` / `ai_shutdown` 在 `ai_service.h` 中是否保留为 deprecated inline wrapper 转发给 `ai_config_provider_*`，由实现者依据老测试用例无缝编译需求自由裁量。
