# 代码审查改进点设计（4 项：死代码 / 汇率单实例假设 / void* 设计意图 / SQLite 最低版本标注）

日期：2026-11-09
状态：已定稿（用户逐段确认）

## 背景

上一轮代码库评价中列出 6 条可改进点，用户选择其中 4 项纳入本次设计（编号沿用上一轮评价）：

| 编号 | 改进点 | 选定范围 |
|---|---|---|
| 1 | `application/transfer/usecases.c` 末尾重复的 `return 0` 死代码 | 仅清理这一处已确认位置（不做全库扫描） |
| 3 | `services/market/exchange_rate_service.c` 的 `g_rates[]` 静态内存表在多实例部署下可能不一致 | 纯文档：在 AGENTS.md 标注"当前为单实例设计"，不做代码改动 |
| 5 | C 层用例函数 `void* pool` 参数 + 内部 `(csilk_db_pool_t*)` 强转牺牲类型安全 | 纯文档：在 AGENTS.md 说明这是刻意设计，不做代码改动 |
| 6 | SQLite 最低版本要求（`RETURNING` 需 3.35+）未在文档中标注 | README.md / AGENTS.md 补注 + `db_init` 启动时打印实际 SQLite 版本（可观测性信号，不阻断启动） |

改进点 2（AI 工具名称模糊匹配）与改进点 4（chat store 竞态）本次不涉及。

## 提交拆分（方案 A）

| commit | 内容 |
|---|---|
| commit 1 | `chore: 清理 transfer usecase 死代码 + 补全文档说明（汇率单实例假设 / SQLite 最低版本 / void* 设计意图）` |
| commit 2 | `feat(db): db_init 启动时打印实际 SQLite 版本作为可观测性信号` |

理由：文档类改动零运行时风险，可立即合入；`db.c` 涉及新增代码路径，单独一个 commit 便于单独回滚或测试覆盖。

## 详细设计

### 改进点 1：`transfer/usecases.c` 死代码清理

文件：`backend/src/application/transfer/usecases.c`

`transfer_usecase_create()` 函数尾部当前为：

```c
    /* Commit */
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }
    out_res->code = 0;
    return 0;
    out_res->code = 0;   /* 死代码：永远不可达 */
    return 0;            /* 死代码：永远不可达 */
}
```

改动：删除最后两行不可达死代码，保留正常返回路径：

```c
    /* Commit */
    if (db_tx_scope_commit(pool, &scope) != 0) {
        db_tx_scope_rollback(pool, &scope);
        out_res->code = 500;
        snprintf(out_res->message, sizeof(out_res->message), "提交事务失败");
        return -1;
    }
    out_res->code = 0;
    return 0;
}
```

影响面：零行为变化（原第二遍 `return 0` 本就不可能执行到），纯清理；只需编译通过验证无语法破坏，不需要跑全量测试。

### 改进点 6（文档部分）：SQLite 最低版本标注

1. `README.md` 技术栈表（数据库行）：

   原文：`| **数据库** | SQLite 3 (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移 |`

   改为：`| **数据库** | SQLite 3.35+ (WAL 模式) / PostgreSQL 16+ | 双引擎通用，支持动态运行时无感数据库迁移；RETURNING 语法要求 SQLite ≥ 3.35 |`

2. `AGENTS.md` "Runtime / Tooling Preferences" 表的 Backend 行下方新增一行：

   `SQLite 最低版本要求：3.35+（18 个 repository 实现文件依赖 `RETURNING` 语法，该特性于 SQLite 3.35 引入）`

### 改进点 3（汇率单实例假设）：AGENTS.md 新增小节

在 `AGENTS.md` 新增小节（建议位置：紧跟"Key Directories"表之后，避免打散该表本身），标题 `## Market / FX — 单实例假设`，正文：

> 汇率同步（`backend/src/services/market/exchange_rate_service.c`）当前按**单实例部署**设计：`g_rates[]` 是进程内静态内存表，`exchange_rate_refresh_all()` 无跨实例协调机制。多副本部署时不同实例的内存表可能不一致（读路径 `exchange_rate_get_to_cny` 优先查内存表，查不到时才回落到 `exchange_rate_history` 表）。
>
> **多副本部署前需改造或明确标注**，可选方向：
> - （a）读路径改为优先查 `exchange_rate_history` 最新一行，消除对内存表的依赖；
> - （b）引入类似 `schema_migration_lock` 的行锁机制做单写者仲裁；
> - （c）保持现状，并在部署文档中明确标注"汇率同步仅支持单实例部署"。
>
> 本次改动仅为文档标注，不做代码改造。

### 改进点 5（`void*` 设计意图）：AGENTS.md 新增小节

在 `AGENTS.md` "Code Conventions & Common Patterns" 章节下（建议插在 `C — Repository Pattern (STRICT)` 小节附近）新增小节，标题 `C — 用例层 void* pool 参数是刻意设计`，正文：

> `application/*/usecases.h` 中所有用例函数签名统一使用 `void* pool`（而非 `csilk_db_pool_t*`），这是 DDD 分层刻意的解耦设计——用例层不直接 include 基础设施层（`csilk_db_pool_t` 定义所在）头文件，仅在 `.c` 实现里做一次性强转 `(csilk_db_pool_t*)pool`。
>
> 这是**有意为之，不是疏漏**，请勿"修复"为强类型签名（改动面涉及 15 个 `usecases.h` 头文件 + 所有调用点，违背当前分层约束）。如需更强类型检查，可参考未来在 `common/db.h` 引入 `typedef csilk_db_pool_t* mf_db_pool_t` 的方案，但当前暂不实施。

### 改进点 6（代码部分）：`db_init` 打印实际 SQLite 版本

文件：`backend/src/common/db.c`，`db_init()` 函数内，`if (!g_is_postgres)` 分支、现有 PRAGMA 设置块之后新增：

```c
    if (!g_is_postgres) {
        csilk_db_exec(g_pool, "PRAGMA journal_mode=WAL;");
        csilk_db_exec(g_pool, "PRAGMA busy_timeout=5000;");
        csilk_db_exec(g_pool, "PRAGMA synchronous=NORMAL;");
        csilk_db_exec(g_pool, "PRAGMA foreign_keys=ON;");
        csilk_db_exec(g_pool, "UPDATE ai_traces SET top_p = 0.0 WHERE typeof(top_p) = 'text';");
        csilk_db_exec(g_pool,
                      "UPDATE ai_traces SET temperature = 0.0 WHERE typeof(temperature) = 'text';");

        /* 可观测性：打印实际链接的 SQLite 库版本；RETURNING 语法要求 3.35+ */
        CSILK_LOG_I(
            "SQLite version: %s (min required 3.35 for RETURNING; "
            "older versions will fail on repository files using RETURNING)",
            sqlite3_libversion());
    }
```

实现说明：
- 直接使用 SQLite C API 的 `sqlite3_libversion()`（`db.c` 顶部已 `#include <sqlite3.h>`），无需再走一次 `csilk_db_exec` 查询。
- 仅打印，不 `return -1`、不阻断启动——低于 3.35 的 SQLite 仍能让非 `RETURNING` 路径正常工作，定位为"可观测性信号"而非"硬门禁"。

影响面：纯新增日志输出，无行为变化、无新依赖、无 SQL 变更；需重新编译 `common/db.c` 并跑一次 `backend/tests/test_link.sh` 确认日志正常打印、无回归。

## 验收标准

1. `backend/src/application/transfer/usecases.c` 中 `transfer_usecase_create()` 末尾无不可达重复 `return 0`。
2. `README.md` 数据库行明确写 `SQLite 3.35+`；`AGENTS.md` Runtime/Tooling 表含 SQLite 最低版本说明。
3. `AGENTS.md` 含 `## Market / FX — 单实例假设` 小节与 `C — 用例层 void* pool 参数是刻意设计` 小节。
4. `backend/src/common/db.c` `db_init()` 的 SQLite 分支启动时打印 `sqlite3_libversion()` 结果；不阻断启动。
5. `cmake --build backend/build --parallel` 通过；`./backend/tests/test_link.sh` 全绿。
6. 按方案 A 拆成 2 个 commit 提交（commit 1 = 死代码 + 文档；commit 2 = `db.c` 打印）。

## 非目标（明确不做）

- 不改 `application/*/usecases.h` 的 `void*` 签名。
- 不改造 `exchange_rate_service.c` 的读/写路径或引入分布式锁。
- 不做全库死代码扫描（仅限 `transfer/usecases.c` 这一处）。
- 不把 SQLite 版本过低处理成启动失败/硬门禁。
