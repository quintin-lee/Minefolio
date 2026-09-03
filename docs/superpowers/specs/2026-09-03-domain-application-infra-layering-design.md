# Minefolio P1-01：Domain / Application / Infrastructure / Interfaces 分层架构设计规范

- **日期**: 2026-09-03
- **模块**: Minefolio Backend (`backend/src/`)
- **阶段**: P1-01 架构分层重构（首期样板：`transaction` 领域）
- **状态**: Approved

---

## 1. 背景与目标

Minefolio 后端采用 C23 开发，原有架构为传统三层架构（Controllers -> Services -> Repositories）。随着金融定点数计算核心（`core/financial`）、统一账本状态引擎（`core/ledger`）及解耦 AI 系统的落地，系统需要建立清晰严谨的 Clean Architecture / 领域驱动设计（DDD）分层模型，以解决以下结构性问题：

1. **业务规则泄露**：部分业务判断、手续费子单不变式分散在控制器和仓储层；
2. **数据载荷耦合**：原有仓储层函数直接返回 `csilk_json_t*` 框架级 JSON 节点，导致业务层与 HTTP/JSON 深度耦合；
3. **职责边界模糊**：Application 事务管理、用例编排与底层 SQL 查询混合。

### 最终分层目录目标

```text
backend/src/
├── core/                     # 金融核心算法与统一账本状态引擎 (已建立)
├── domain/                   # 纯业务领域模型、实体、值对象、规则与仓储契约
├── application/              # 业务用例编排、命令 (Commands)、事务边界与 DTO
├── infrastructure/           # 基础设施实现：数据库访问、仓储 SQL 实现、加密与外部服务
├── interfaces/               # 表现层接入：HTTP 控制器、中间件、路由装配
└── main.c                    # 服务装配主入口
```

---

## 2. 领域层设计规范 (Domain Layer)

### 2.1 纯净性强约束 (Purity Invariants)
- **编译时绝对禁止依赖**：
  - HTTP Server / `csilk/csilk.h`；
  - SQLite3 (`sqlite3.h`) / PostgreSQL (`libpq-fe.h`)；
  - JSON 框架 (`csilk_json_t` / `cJSON`)；
- **允许依赖**：
  - C 标准库 (`stdint.h`, `stdbool.h`, `string.h`, `math.h` 等)；
  - 金融核心库 `core/financial/` (`money.h`, `quantity.h`, `price.h`, `rate.h`, `pnl.h`)。

### 2.2 实体与值对象 (`domain/transaction/entity.h`)
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core/financial/money.h"
#include "core/financial/quantity.h"
#include "core/financial/price.h"

typedef struct mf_transaction {
    int64_t         id;
    int64_t         user_id;
    int64_t         asset_id;         /* 投资标的资产 ID (非投资类型为 0) */
    int64_t         account_id;       /* 资金账户 ID */
    int64_t         parent_tx_id;     /* 手续费子单关联的主交易 ID (无父单为 0) */
    char            type[32];         /* buy, sell, deposit, withdraw, fee, dividend 等 */
    mf_quantity_t   amount;           /* 交易数量/份额 */
    mf_price_t      price;            /* 单价 */
    mf_money_t      fee;              /* 手续费金额 */
    char            fee_currency[8];  /* 手续费币种 */
    char            note[256];        /* 备注 */
    char            tx_time[64];      /* 交易发生时间 (ISO 8601) */
    char            created_at[64];
    char            updated_at[64];
} mf_transaction_t;

bool mf_tx_is_investment(const mf_transaction_t* tx);
bool mf_tx_is_fee_child(const mf_transaction_t* tx);
bool mf_tx_has_fee(const mf_transaction_t* tx);
```

### 2.3 纯 C 仓储契约接口 (`domain/transaction/repository.h`)
```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "domain/transaction/entity.h"

int  mf_tx_repo_find_by_id(void* db_pool, int64_t user_id, int64_t id, mf_transaction_t* out_tx);
int  mf_tx_repo_save(void* db_pool, const mf_transaction_t* tx, int64_t* out_id);
int  mf_tx_repo_update(void* db_pool, const mf_transaction_t* tx);
int  mf_tx_repo_delete(void* db_pool, int64_t user_id, int64_t id);

int  mf_tx_repo_find_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id,
                                  mf_transaction_t** out_list, size_t* out_count);
int  mf_tx_repo_delete_fee_children(void* db_pool, int64_t user_id, int64_t parent_tx_id);
void mf_tx_repo_free_list(mf_transaction_t* list, size_t count);
```

### 2.4 领域业务规则与不变式 (`domain/transaction/rules.h/.c`)
- `mf_tx_rule_validate(const mf_transaction_t* tx, char* err_buf, size_t err_len)`：
  - 交易类型合法性（通过 `tx_type_lookup` 匹配已知类型）；
  - 投资交易买卖必须具备有效标的资产（`asset_id > 0`）与正成交价格（`price > 0`）；
  - 交易数量与费用非负校验。
- `mf_tx_rule_build_fee_child(const mf_transaction_t* parent, mf_transaction_t* out_fee)`：
  - 当主交易 `fee > 0` 时，自动生成手续费子交易实体；
  - 绑定 `parent_tx_id = parent->id`，强制非空备注（原备注包含 `'fee'` 或保底 `'fee'`）。

---

## 3. 应用层设计规范 (Application Layer)

### 3.1 职责边界
- 负责用例编排（Use Cases）、命令结构（Commands）与返回值 DTO；
- 控制数据库事务原子性边界（`BEGIN TRANSACTION / COMMIT / ROLLBACK`）；
- 协调调用账本引擎状态演进（`ledger_engine_apply`）与资金余额（`balance_apply_delta`）；
- **严禁直接编写任何 SQL 语句**。

### 3.2 命令与用例定义 (`application/transaction/`)
- `commands.h`：定义 `create_tx_cmd_t`、`delete_tx_cmd_t`、`query_tx_filter_t`；
- `dtos.h`：定义 `tx_usecase_result_t`、`tx_dto_t`、`tx_page_dto_t`；
- `usecases.h/.c`：
  - `tx_usecase_create(void* pool, const create_tx_cmd_t* cmd, tx_usecase_result_t* out_res)`；
  - `tx_usecase_delete(void* pool, const delete_tx_cmd_t* cmd, tx_usecase_result_t* out_res)`；
  - `tx_usecase_query(void* pool, const query_tx_filter_t* filter, tx_usecase_result_t* out_res)`。

---

## 4. 基础设施层设计规范 (Infrastructure Layer)

### 4.1 职责边界
- 负责数据库实际访问、连接池管理与物理存储映射；
- 实现 `domain/transaction/repository.h` 中声明的纯 C 仓储契约；
- **严禁包含手续费计算、正负号翻转等业务决策逻辑**。

### 4.2 仓储实现 (`infrastructure/repositories/transaction_repo_impl.c`)
- 所有 SQL 语句使用命名预编译与 `?` 严格参数化绑定；
- 从 SQL 结果集读取数据（`db_get_num`、`db_get_str`），构造回填为 `mf_transaction_t` 纯 C 结构体。

---

## 5. 接口表现层设计规范 (Interfaces Layer)

### 5.1 职责边界
- 极薄 HTTP 控制器；
- 负责 HTTP 报文解析（Query Params、JSON Body）、JWT 用户上下文获取；
- 组装 Application 命令并调用用例，将用例结果映射封装为标准 HTTP JSON Envelope（`respond_ok`、`respond_error`）；
- **严禁包含任何业务逻辑**。

---

## 6. 平滑迁移与兼容门面策略 (Migration & Compatibility Facade)

### 6.1 迁移次序
按照用户指令分阶段逐个域迁移：
1. **`transaction` (P1-01 本期样板)**
2. `asset`
3. `portfolio`
4. `cashflow`
5. `market`
6. `auth`
7. `AI`

### 6.2 现有服务门面保证
保留原有的 `backend/src/services/transaction_service.c`、`transaction_write.c`、`transaction_query.c` 作为轻量门面，其内部逻辑改为调用 `application/transaction/usecases.h`。外部既有路由注册和 7 大测试套件无感知。

---

## 7. 验收测试标准

1. **Domain 隔离编译验证**：`backend/tests/unit/test_domain_transaction.c` 编译链接时，不引入 SQLite、PostgreSQL、JSON 与 HTTP 库；
2. **Application 零 SQL 静态检查**：`grep -E "(SELECT|INSERT|UPDATE|DELETE)" backend/src/application/transaction/*` 结果为 0；
3. **CTest 单元测试全部通过**：覆盖金融定点数、账本引擎、AI、Secret Provider 及新增 Domain 实体测试；
4. **端到端集成测试全量通过**：`./tests/test_link.sh` 38 项用例（139 项真实断言）100% PASS。
