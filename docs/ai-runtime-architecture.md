# Minefolio Unified AI Runtime 架构设计与规范说明书

## 1. 架构总览 (Architecture Overview)

为了彻底解决上层业务模块（HTTP 控制器、工作流执行引擎、领域工具集）与底层大语言模型（LLM）直接耦合、缺乏统一上下文管理、缺少主动死循环与预算熔断保护、无法可靠防止长对话 Token 溢出等问题，Minefolio 建立了全新、高内聚、模块化的 **Unified AI Runtime**（统一智能运行时）。

### 核心设计原则

1. **执行权统一收敛**：业务控制器 (`ai_controller.c` / `interfaces/http/controllers/ai_controller.c`) 与工作流引擎 (`workflow/executor.c`) **严禁直接调用** LLM 驱动，所有对话生成与流式任务必须委托给 `ai_runtime_execute_stream()` 或 `ai_runtime_execute()`。
2. **状态与边界容器化**：统一通过 `ai_runtime_context_t` 封装用户信息、会话 ID、消息历史、工具列表、鉴权凭据、预算限制与全链路 Trace。
3. **分层安全风控门禁**：工具调用不可直接穿透到数据库，必须通过 `ai_policy_evaluate()` 经过权限校验、五级金融风控（NORMAL ~ CRITICAL）与带 Nonce 防重放的双重确认令牌机制。
4. **硬配额与主动防御**：原生支持迭代轮数上限 (`max_iterations`)、硬超时 (`timeout_ms`)、Token 配额 (`token_budget`)、工具调用配额 (`tool_budget`)、美元成本配额 (`cost_budget`) 与协作式取消 (`cancellation_token`)。
5. **记忆滑动窗口防溢出**：通过 `ai_memory_build_messages()` 在内存受限下截断超长历史消息，同时保证系统提示词（System Prompt）永久置顶不被丢弃。
6. **结构化错误分类**：建立 `AI_RUNTIME_ERR_*` 细粒度故障体系，清晰区分模型网络错误、工具执行错误、风控拦截、超时与取消。

---

## 2. 运行时子系统目录结构

```text
backend/src/services/ai/
├── runtime/              # 核心运行时状态机与统一容器
│   ├── context.h / .c    # 统一上下文容器 (ai_runtime_context_t) 初始化与销毁
│   ├── loop.h / .c       # Agent 执行循环核心状态机 (Execute Stream / Batch)
│   ├── limits.h / .c     # 运行预算配额校验与统计跟踪 (Limits & Budgets)
│   ├── error.h / .c      # 结构化错误分类 (Error Taxonomy) 与状态对象
│   ├── session.h / .c    # 会话元数据持久化、自动标题提炼与历史检索
│   └── runtime.h / .c    # 运行时公共声明与跨模块聚合
├── memory/               # 上下文记忆与窗口管理
│   └── memory.h / .c     # 滑动窗口消息截断、系统提示词锁定与 Token 估算
├── model/                # 模型提供者抽象与流式解析
│   ├── model.h / .c      # 模型配置、请求组装与流式请求转发
│   ├── provider.h / .c   # DeepSeek / OpenAI 兼容提供商适配
│   ├── request.h / .c    # 提示词组织与 JSON Payload 构建
│   └── response.h / .c   # SSE 流式 Chunk 解码与 JSON 解析
├── tools/                # 领域工具注册表与 Schema 校验
│   ├── registry.h / .c   # 工具注册表、入参类型反射与 Schema 生成
│   ├── dispatcher.h / .c # 工具调用路由与上下文环境注入
│   ├── asset_tool.c      # 资产与余额查询工具 (get_assets 支持可选 account_id 过滤; 结果含 account_name/institution)
│   ├── transaction_tool.c# 交易查询与记账草案工具
│   ├── expense_tool.c    # 收支明细记录工具
│   ├── cashflow_tool.c   # 现金流排程与预测工具
│   ├── portfolio_tool.c  # 投资组合表现与权重分析工具
│   └── report_tool.c     # 多币种汇总与外汇损益报告工具
├── policy/               # 安全策略、风控与防重放
│   ├── policy.h / .c     # 策略评估入口 (ai_policy_evaluate)
│   ├── risk.h / .c       # 5 级金融风险矩阵评定
│   ├── confirmation.h/.c # HMAC-SHA256 签名、防重放 Nonce 缓存与二次确认令牌
│   ├── permission.h / .c # 用户角色与多账本空间操作鉴权
│   └── audit.h / .c      # 审计日志快照与敏感凭据自动脱敏
├── trace/                # 全链路追踪与可观测性
│   ├── trace.h / .c      # OpenTelemetry 风格 Span、延迟统计与 Token 导出
│   └── exporter.h / .c   # 异步持久化写入 ai_traces 数据库表
└── workflow/             # 高阶多步 DAG 编排引擎
    ├── graph.h / .c      # 工作流 DAG 有向无环图拓扑构建
    ├── executor.h / .c   # 节点状态机迁移与执行编排（委托至 Runtime）
    └── workflows/        # 内置场景报告模板 (财务体检、现金流预测、组合诊断)
```

---

## 3. Agent 核心执行循环状态机 (Agent Loop)

Agent 循环是 Runtime 最核心的状态机引擎，其完整的状态转移生命周期如下：

```
                [Start: ai_runtime_execute_stream]
                               │
                               ▼
                    [Load / Build Context]
                               │
                               ▼
                 [Memory Sliding Window Pruning]
             (保留 System Prompt，按 Token 预算修剪历史)
                               │
                               ▼
        ┌──────────────► [Pre-Turn Limits Check]
        │              (检查超时/迭代轮数/Token/取消信号)
        │                      │
        │             超限? ───┴─── 是 ──► [Set AI_RUNTIME_ERR_*] ──► [Finish]
        │                      │ 否
        │                      ▼
        │              [Call Model Stream]
        │              (推送 SSE Delta Chunk)
        │                      │
        │                      ▼
        │             [Parse Model Response]
        │                      │
        │         包含 Tool Calls? ── 否 ──► [Append Assistant Msg] ──► [Finish]
        │                      │ 是
        │                      ▼
        │            [Tool Batch Eligibility Check]
        │         (全为无状态 Web 工具? -> 并行工作组; 含 DB/写操作? -> 串行保序)
        │                      │
        │                      ▼
        │          [Security Policy & De-dup Check]
        │                      │
        │         风控拦截/需确认? ── 是 ──► [Emit Confirmation Draft] ──► [Finish]
        │                      │ 否
        │                      ▼
        │       [Execute Tools (Parallel / Sequential)]
        │                      │
        │                      ▼
        │             [Record Tool Spans]
        │                      │
        │                      ▼
        │      [Append Tool Results (Ordered Barrier)]
        │                      │
        └──────────────────────┘
```

---

## 4. 上下文容器 (Unified Runtime Context)

`ai_runtime_context_t` 结构体聚合了一次运行生命周期所需的全部资源：

```c
typedef struct ai_runtime_context {
    int64_t                user_id;        // 触发会话的用户 ID
    int64_t                session_id;     // 会话唯一标识 (0 代表临时或工作流执行)
    csilk_json_t*          messages;       // 当前上下文全部对话消息 (JSON 数组)
    ai_tool_registry_t*    tools;          // 启用的工具注册表指针
    csilk_db_pool_t*       pool;           // 数据库连接池
    ai_runtime_limits_t    limits;         // 运行时硬预算与安全配额
    ai_runtime_stats_t     stats;          // 当前累计统计指标 (耗时/轮数/Tokens/费用)
    ai_trace_context_t*    trace;          // 全链路追踪 Span 上下文
    char                   model_name[64]; // 模型标识 (如 deepseek-chat, gpt-4o-mini)
    char                   custom_system_prompt[1024]; // 动态自定义系统提示词
} ai_runtime_context_t;
```

---

## 5. 预算与硬限制 (Limits & Budgets)

为避免模型陷入幻觉引起的死循环，或恶意调用高耗费接口，Runtime 默认启用严格的防御预算：

| 配额项 | 结构体字段 | 默认值 | 校验逻辑 | 违规错误 |
|---|---|---|---|---|
| **迭代轮数上限** | `max_iterations` | `10` | 状态机循环计数 $\ge$ 上限即终止 | `AI_RUNTIME_ERR_TIMEOUT` / 迭代耗尽 |
| **硬超时保护** | `timeout_ms` | `120,000ms` (2分钟) | 每轮循环前比对墙上时钟 `(now - start_ms)` | `AI_RUNTIME_ERR_TIMEOUT` |
| **Token 预算** | `token_budget` | `32,768` Tokens | 累计输入+输出 Tokens 超过预算则拒绝下一轮 | `AI_RUNTIME_ERR_CONTEXT_OVERFLOW` |
| **工具调用上限** | `tool_budget` | `20` 次 | 累计执行工具总数超过配额立即熔断 | `AI_RUNTIME_ERR_TOOL` |
| **成本预算控制** | `cost_budget` | `$1.00` 美元 | 按模型单价折算的总成本超过配额立即停止 | `AI_RUNTIME_ERR_TIMEOUT` |
| **协作式中断信号** | `cancellation_token` | `const bool*` | 客户端断开连接或用户点击停止时置位 | `AI_RUNTIME_ERR_CANCELLED` |

---

## 6. 上下文滑动窗口记忆 (Memory Window)

为了防止长对话上下文无限增长导致 LLM 抛出 400 Bad Request，`services/ai/memory/` 提供了智能滑动窗口管理算法：

1. **系统提示词锁定**：遍历消息列表，检测首条或所有 `role == "system"` 的消息，予以硬锁定（Pin），绝不参与截断。
2. **最新消息优先**：从最新消息逆序向前统计 Token 预算，当达到 `token_budget - SYSTEM_PROMPT_RESERVED` 阈值时，自动截断更早的历史消息。
3. **对话轮次配对**：截断时优先保持 `user` 与 `assistant` 轮次成对截取，避免出现孤立的 `assistant` 或 `tool` 结果消息。

---

## 7. 结构化错误分类 (Error Taxonomy)

所有的故障在 Runtime 层都被映射为确定性的状态枚举 `ai_runtime_error_t`：

```c
typedef enum {
    AI_RUNTIME_ERR_OK               = 0, // 正常完成
    AI_RUNTIME_ERR_MODEL            = 1, // 模型网络不可达、鉴权失败或响应解析错误
    AI_RUNTIME_ERR_TOOL             = 2, // 工具执行内部异常或参数校验失败
    AI_RUNTIME_ERR_POLICY           = 3, // 权限不足或被五级风控拦截
    AI_RUNTIME_ERR_TIMEOUT          = 4, // 运行超时或超出最大迭代轮数
    AI_RUNTIME_ERR_CONTEXT_OVERFLOW = 5, // 消息上下文超出 Token 预算
    AI_RUNTIME_ERR_VALIDATION       = 6, // 运行时输入参数校验不合法
    AI_RUNTIME_ERR_CANCELLED        = 7  // 客户端主动取消或中断
} ai_runtime_error_t;
```

---

## 8. 深度思考推理流与持久化 (Thinking Mode & Reasoning Stream)

为了原生支持 DeepSeek-R1、OpenAI o-series 以及 Anthropic Claude 等具有显式慢思考思维链（Chain of Thought）的推理模型，AI Runtime 与 SSE 协议栈提供了深度思考专用传输与存储管道：

### 8.1 SSE 流式事件协议扩展
在模型流式回复期间，推理 Chunk 以独立事件 `type: "thinking"` 实时分发，客户端收到后可与正式回复正文解耦渲染：
```
event: delta
data: {"type": "thinking", "content": "正在检索用户过去30天的日常支出明细与资产持仓..."}

event: delta
data: {"type": "delta", "content": "根据您的账本统计，上月总支出为..."}
```

### 8.2 运行时状态暂存与数据库落库
- **运行时缓冲**：Agent Loop (`runtime/loop.c`) 在流式迭代中维护动态扩容的 `thinking_buffer`；
- **数据库表扩展 (V015)**：Migration `V015__ai_messages_thinking_content.sql` 在 `ai_messages` 表中增加了 `thinking_content TEXT` 字段；
- **数据库表扩展 (V016)**：Migration `V016__ai_messages_tool_calls.sql` 在 `ai_messages` 表中增加了 `tool_calls TEXT DEFAULT ''` 字段，用于持久化模型返回的工具调用记录（JSON 数组），供前端回显工具执行状态与 Agent Loop 上下文重建；
- **账户维度 Schema (V017)**：Migration `V017__accounts_table.sql` 引入 `accounts` 表与 `assets.account_id` 外键；AI 资产工具 (`exec_get_assets`) 的查询结果随之携带 `account_name` / `institution` 字段，并支持可选 `account_id` 整型入参过滤，Agent 可按账户维度检索与汇总资产（`V001`~`V017` 自动迁移基线）。
- **原子持久化**：当一轮对话完成时，`mf_ai_session_repo_save_message_with_thinking()` 将正文回复、完整的思考过程与 `tool_calls` JSON 原子持久化入库，保证历史会话在重新载入时能够完整回溯思维链与工具调用轨迹。

---

## 10. MCP 客户端与进程沙箱加固 (MCP Client Hardening)

位于 `services/ai/tools/mcp/mcp_client.c`：
1. **4KB 块缓冲读取器 (`mcp_io_reader_t`)**：采用 4KB 局部缓冲与 `memchr` 内存扫描换行分帧，彻底消除 100K 级别的逐字节单系统调用；单帧超过 1MB 时主动熔断报错，防止畸形数据打爆内存。
2. **双描述符 Polling 与非阻塞 Stderr 排空**：`poll()` 同时监听 stdout (`read_fd`) 与 stderr (`err_fd`)。通过 `drain_stderr_nonblocking` 实时排空子进程标准错误，彻底防止外部 MCP 脚本输出大量诊断日志打满 64KB 内核管道而导致写阻塞死锁；对端关闭时安全重置 `err_fd = -1` 避免 `POLLHUP` CPU 100% 忙轮询。
3. **子进程 post-dup2 隔离沙箱 (`closefrom`)**：子进程在 `dup2(0, 1, 2)` 重定向标准文件描述符后，立即调用 `closefrom(3)`（支持扫描 `/proc/self/fd/` 兜底），杜绝父进程的 HTTP 监听套接字与数据库连接池泄漏至第三方工具子进程。

---

## 11. 异步记忆 Worker 线程池 (Summary Thread Pool)

位于 `services/ai/memory/summary.c`：
1. **常驻 2-Worker 线程池**：淘汰按需裸起 `pthread_create` 的模式，常驻 2 个背景 Worker 线程消费环形任务队列（容量 64）。
2. **64 槽 LRU 暂存队列**：待落库摘要暂存区从 16 槽扩容至 64 槽，支持同一 `session_id` 覆盖更新与全满时基于 `created_at` 时间戳的 LRU 淘汰保底。
3. **零跨线程 DB 竞争**：Worker 线程在内存中完成大模型推理并放入暂存区；数据库写入完全由主请求线程在 `ai_summary_flush_pending()` 串行执行，确保 SQLite 单连接绝对线程安全。
4. **生命周期管理**：在 `ai_init()` 首行幂等初始化，在 `ai_shutdown()` 中广播退出并完整回收队列与内存，实现 AddressSanitizer 零内存泄漏。

---

## 12. 确定性工作流节点容错与错误显式传播 (Workflow Step Error)

位于 `services/ai/workflow/executor.c`：
1. **拒绝静默掩盖**：确定性前置节点（如月度收支汇总、投资组合计算）返回 `NULL` 或畸形 JSON 时，执行器主动拦截，释放当前上下文。
2. **协议对齐的 SSE 错误通知**：向客户端推送包含 `type: "error"`、`step_index`、`step_id`、`message` 的 SSE 事件，并返回 `-1` 立即终止流程，避免下游终态大模型依据残缺上下文胡乱生成财务建议，并节约 Token。

---

## 13. 测试与验证矩阵 (Verification Matrix)

AI Runtime 体系拥有 100% 覆盖的单元测试与真实的端到端集成测试验证：

1. **CTest 单元测试套件 (`tests/unit/`)**：
   - 上下文容器初始化与自动释放测试 (`test_context_lifecycle`)
   - 错误码枚举与结构化状态对象转换 (`test_error_taxonomy`)
   - 运行预算校验与统计累加器 (`test_limits_and_budgets`)
   - 记忆滑动窗口与系统提示词保留截断 (`test_memory_sliding_window`)
   - 超时、超限与外部中断信号模拟 (`test_runtime_cancellation_and_timeouts`)
   - MCP 4KB 块缓冲读取与 1MB 上限截断 (`test_mcp_client_io`)
   - MCP Stderr 非阻塞排空与防管道死锁 (`test_mcp_stderr_capture`)
   - MCP 子进程 post-dup2 `closefrom(3)` 描述符隔离 (`test_mcp_child_sanitation`)
   - 无状态网络工具并行调度 vs 串行回退 (`test_ai_parallel_tools`)
   - 确定性工作流节点错误显式传播与阻断 (`test_workflow_step_error`)
   - 异步记忆 Worker 线程池入队、LRU 淘汰与清理 (`test_summary_worker_pool`)
2. **端到端集成回归**：
   - 验证真实会话创建、多轮对话流式返回、工具 Span 上报与 `ai_traces` 表落库统计 (`test_ai_trace.sh`)
   - 验证超时重试、预算收敛总结、去重拦截与动账令牌强制拦截 (`test_ai_loop_hardening.sh`)
   - 验证 HTTP 与 STDIO 模式下的 MCP 服务端点通信与工具调用 (`test_mcp.sh`)

