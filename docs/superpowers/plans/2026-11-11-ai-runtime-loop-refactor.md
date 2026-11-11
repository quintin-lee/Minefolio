# AI Runtime loop.c 子流程拆解与重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

本计划旨在将 `backend/src/services/ai/runtime/loop.c` 中过于臃肿的 `ai_runtime_execute_stream`（~800 行大单体函数）重构拆解为 4 个清晰独立的静态纯函数子流程：“参数预处理”、“并发任务打包”、“响应结果解析”、“追踪日志上报”，显著精简主循环上下文中的局部变量数量与心智负担。

---

## 总体设计与模块切分

### 1. 文件与接口变化
- **修改文件**：[`backend/src/services/ai/runtime/loop.c`](file:///data/home/quintin/workspace/source/c/Minefolio/backend/src/services/ai/runtime/loop.c)
- **外部暴露接口**：保持不变（`ai_runtime_execute_stream`、`ai_runtime_execute`、`ai_runtime_run_loop` 外部签名与语义 100% 保持一致）。

### 2. 子流程与数据结构映射

| 阶段 | 拆解子流程函数 | 职责与消减的局部变量 |
| :--- | :--- | :--- |
| **阶段一：参数预处理** | `loop_prepare_env`<br>`loop_cleanup_env` | 校验 Context、获取配置快照、匹配 Provider、计算模型生效价格、创建驱动实例 `csilk_ai_t`、合并 MCP 工具、装配注入防御 system prompt、初始化初始消息数组与 Trace。消除 `cfg`, `prov`, `owner_sys_prompt`, `eff_price_in`, `eff_price_out`, `mcp_merged`, `ai_inst` 等 10+ 局部变量。 |
| **阶段二：响应解析与持久化** | `loop_persist_assistant_message`<br>`loop_append_assistant_tool_message`<br>`loop_execute_budget_wrap` | 统一抽象 assistant 文本/思考/工具调用落库（消除 3 处冗余重复代码）；抽象模型工具调用追加到 `msgs`；抽象收敛调用（wrap call）。 |
| **阶段三：并发与顺序工具打包** | `loop_dispatch_parallel_tools`<br>`loop_dispatch_sequential_tools` | 隔离 8 个 Worker 线程的生命周期、主线程安全策略预校验、同参去重哈希匹配、保序屏障汇聚、工具输出夹逼与隔离脱敏。将 `tasks[8]`, `threads[8]`, `spawned[8]` 从主函数彻底消除。 |
| **阶段四：追踪结算与终态通知** | `loop_finalize_trace`<br>`loop_notify_final_status` | 追踪价格与耗时结算、Token 成本计算、终态落库与自动销毁、`on_done`/`on_error` 回调通知。 |

---

## 阶段任务清单 (Tasks)

### Task 1: 提取参数预处理与执行环境管理 (`loop_prepare_env` / `loop_cleanup_env`)

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c`

- [ ] **Step 1: 定义执行环境容器结构体 `loop_exec_env_t`**
  包含：
  ```c
  typedef struct {
      ai_config_t*           cfg;
      ai_provider_t*         prov;
      const char*            model;
      double                 eff_price_in;
      double                 eff_price_out;
      csilk_ai_t*            ai_inst;
      const csilk_ai_tool_t* tools;
      size_t                 tool_count;
      int                    mcp_merged;
      csilk_ai_message_t*    msgs;
      size_t                 mc;
      size_t                 initial_mc;
      char*                  owner_sys_prompt;
      ai_trace_t*            trace;
      int                    auto_created_trace;
      int                    http_timeout_ms;
  } loop_exec_env_t;
  ```

- [ ] **Step 2: 实现静态函数 `loop_prepare_env`**
  抽取 Context 参数有效性校验、Provider 与 API Key 匹配、生效价格计算、`csilk_ai_new` 实例初始化、MCP 工具合并、注入防御 Prompt 拼接、消息初始分配、Trace 初始化。任何失败分支内部安全释放已分配资源并返回 `ai_runtime_status_t`。

- [ ] **Step 3: 实现静态函数 `loop_cleanup_env`**
  统一释放 `ai_inst`, `mcp_bridge_free_merged` (若 mcp_merged), 后续扩展的 `msgs` 元素，`msgs` 容器数组，`owner_sys_prompt`，以及调用 `ai_config_release(cfg)`。

- [ ] **Step 4: 在 `ai_runtime_execute_stream` 中接入并编译验证**
  运行：`cmake --build backend/build --target test_ai_runtime && ./backend/build/test_ai_runtime`
  确保通过。

- [ ] **Step 5: 提交 Task 1 代码**
  `git commit -m "refactor(ai): 🧩 extract loop_prepare_env and loop_cleanup_env in loop.c"`

---

### Task 2: 提取响应解析、消息持久化与收敛调用

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c`

- [ ] **Step 1: 实现统一的消息落库辅助函数 `loop_persist_assistant_message`**
  ```c
  static int64_t
  loop_persist_assistant_message(csilk_db_pool_t*              pool,
                                 int64_t                       session_id,
                                 const char*                   model,
                                 const char*                   content,
                                 const char*                   thinking,
                                 csilk_json_t*                 tc_record,
                                 const ai_runtime_callbacks_t* cbs,
                                 void*                         user_data);
  ```
  封装序列化 `tc_record`、调用 `mf_ai_message_insert`、触发 `cbs->on_message_persisted` 并释放临时 JSON 字符串。

- [ ] **Step 2: 实现 assistant 消息与工具调用追加函数 `loop_append_assistant_tool_message`**
  ```c
  static size_t
  loop_append_assistant_tool_message(csilk_ai_message_t** msgs_ptr,
                                     size_t*              mc_ptr,
                                     const char*          content,
                                     csilk_ai_tool_call_t* tool_calls,
                                     size_t               tool_call_count);
  ```
  返回 `asst_idx` 下标，消除主循环中的 `realloc` + 手动深拷贝代码。

- [ ] **Step 3: 实现预算耗尽时的收敛调用辅助函数 `loop_execute_budget_wrap`**
  封装 lines 1085-1145 的 wrap 请求构建、调用、token 统计与文本持久化。

- [ ] **Step 4: 编译并执行单元测试**
  运行：`cmake --build backend/build --target test_ai_runtime test_ai_token_compact && ./backend/build/test_ai_runtime && ./backend/build/test_ai_token_compact`
  确保通过。

- [ ] **Step 5: 提交 Task 2 代码**
  `git commit -m "refactor(ai): 🧩 extract response persistence and budget wrap helper in loop.c"`

---

### Task 3: 提取并发任务打包与顺序工具分发

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c`

- [ ] **Step 1: 实现并发只读网络工具分发函数 `loop_dispatch_parallel_tools`**
  ```c
  static void
  loop_dispatch_parallel_tools(ai_runtime_context_t*         ctx,
                               csilk_ai_tool_call_t*         tool_calls,
                               size_t                        n_calls,
                               ai_trace_t*                   trace,
                               const ai_runtime_callbacks_t* cbs,
                               void*                         user_data,
                               csilk_json_t*                 tc_record,
                               uint64_t*                     seen_hashes,
                               size_t*                       seen_count,
                               csilk_ai_message_t**          msgs_ptr,
                               size_t*                       mc_ptr);
  ```
  包含 3 个子阶段：主线程参数解析与 Policy 预校验、Worker 线程并发执行、主线程保序汇聚屏障（Trace span 上报、回调通知、tc_record 累加、夹逼与隔离脱敏、追加 role="tool" 消息与任务清理）。

- [ ] **Step 2: 实现常规顺序工具安全分发函数 `loop_dispatch_sequential_tools`**
  ```c
  static void
  loop_dispatch_sequential_tools(csilk_db_pool_t*              pool,
                                 ai_runtime_context_t*         ctx,
                                 csilk_ai_tool_call_t*         tool_calls,
                                 size_t                        n_calls,
                                 ai_trace_t*                   trace,
                                 const ai_runtime_callbacks_t* cbs,
                                 void*                         user_data,
                                 csilk_json_t*                 tc_record,
                                 uint64_t*                     seen_hashes,
                                 size_t*                       seen_count,
                                 csilk_ai_message_t**          msgs_ptr,
                                 size_t*                       mc_ptr);
  ```
  包含单工具的安全策略评测、二次确认拦截、取消检查、工具执行与计时、Trace span 记录、脱敏与 role="tool" 消息追加。

- [ ] **Step 3: 替换主循环中的大分块工具分发逻辑**
  主循环只保留清晰的判断：
  ```c
  if (batch_eligible) {
      loop_dispatch_parallel_tools(...);
  } else {
      loop_dispatch_sequential_tools(...);
  }
  ```

- [ ] **Step 4: 编译并执行并发与策略测试**
  运行：`cmake --build backend/build --target test_ai_parallel_tools test_ai_policy test_policy_mcp_risk && ./backend/build/test_ai_parallel_tools && ./backend/build/test_ai_policy && ./backend/build/test_policy_mcp_risk`
  确保通过。

- [ ] **Step 5: 提交 Task 3 代码**
  `git commit -m "refactor(ai): 🧩 extract parallel and sequential tool dispatch pipelines in loop.c"`

---

### Task 4: 提取追踪日志结算与终态通知

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c`

- [ ] **Step 1: 实现链路追踪结算函数 `loop_finalize_trace`**
  ```c
  static void
  loop_finalize_trace(ai_trace_t*          trace,
                      int                  auto_created_trace,
                      csilk_db_pool_t*     pool,
                      double               eff_price_in,
                      double               eff_price_out,
                      int64_t              prompt_tokens,
                      int64_t              completion_tokens,
                      ai_runtime_status_t* status);
  ```
  负责价格口径绑定、tokens/cost 汇总、`ai_trace_finish` 状态标记、数据库落库持久化，以及对自动创建对象的安全释放。

- [ ] **Step 2: 实现终态事件通知函数 `loop_notify_final_status`**
  ```c
  static void
  loop_notify_final_status(const ai_runtime_status_t*    status,
                           const ai_runtime_stats_t*     stats,
                           const ai_runtime_callbacks_t* cbs,
                           void*                         user_data);
  ```
  根据 `status->code` 状态统一分发 `on_done` 或 `on_error`。

- [ ] **Step 3: 在 `ai_runtime_execute_stream` 尾部收敛接入**

- [ ] **Step 4: 编译并验证**
  运行：`cmake --build backend/build --target test_ai_runtime && ./backend/build/test_ai_runtime`

- [ ] **Step 5: 提交 Task 4 代码**
  `git commit -m "refactor(ai): 🧩 extract trace finalization and status notification in loop.c"`

---

### Task 5: 全面回归验证与全量测试套件检验

- [ ] **Step 1: 执行全量 55 个 CTest 单元测试套件**
  `cd backend/build && ctest --output-on-failure`
  确保 100% 全部通过。

- [ ] **Step 2: 执行 AI 关键端到端脚本测试**
  `./backend/tests/test_ai_loop_hardening.sh`
  `./backend/tests/test_ai_trace.sh`
  `./backend/tests/test_ai_tool_call.sh`

- [ ] **Step 3: 运行前端回归测试确保 SSE 对话与微卡完全兼容**
  `npm --prefix frontend test -- --run`

- [ ] **Step 4: 提交 Task 5 最终集成确认**
  `git commit -m "refactor(ai): 🚀 streamline ai_runtime_execute_stream and verify full test suites"`
