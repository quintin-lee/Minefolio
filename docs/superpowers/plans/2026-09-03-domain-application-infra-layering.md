# Domain / Application / Infrastructure / Interfaces 分层架构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立 Minefolio 后端 Clean Architecture 四层目录（`domain/`、`application/`、`infrastructure/`、`interfaces/`），并将排序第 1 位的核心交易记账域（`transaction`）完整迁移落地，保证 Domain 纯净性与旧 Service 门面兼容性。

**Architecture:** 采用头文件契约倒置（Header Contract Inversion）与命令用例模式。Domain 仅依赖 `core/financial` 纯 C 定点数；Application 负责事务边界与用例编排且零 SQL；Infrastructure 承接仓储具体 SQL 实现与实体映射；Interfaces 作为极薄 HTTP 控制器；现有 Service 作为过渡门面确保现有 7 大测试套件 100% 兼容。

**Tech Stack:** C23, CMake, Csilk, SQLite3 / PostgreSQL, CTest.

---

### Task 1: 建立 Domain 交易领域实体、规则与仓储契约 (`domain/transaction/`)

**Files:**
- Create: `backend/src/domain/transaction/entity.h`
- Create: `backend/src/domain/transaction/repository.h`
- Create: `backend/src/domain/transaction/rules.h`
- Create: `backend/src/domain/transaction/rules.c`
- Create: `backend/tests/unit/test_domain_transaction.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写 Domain 单元测试 (TDD)**

在 `backend/tests/unit/test_domain_transaction.c` 中编写纯 C 领域的业务实体与规则断言（验证其无需链接 SQLite、PostgreSQL、JSON 与 HTTP 库）：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "domain/transaction/entity.h"
#include "domain/transaction/rules.h"

static void test_transaction_entity_and_rules(void) {
    mf_transaction_t tx = {0};
    tx.id = 100;
    tx.user_id = 1;
    tx.asset_id = 2;
    tx.account_id = 3;
    tx.amount = mf_quantity_from_double(10.5);
    tx.price = mf_price_from_double(100.0);
    tx.fee = mf_money_from_double(5.0);
    snprintf(tx.type, sizeof(tx.type), "buy");
    snprintf(tx.note, sizeof(tx.note), "买入测试标的");

    assert(mf_tx_is_investment(&tx) == true);
    assert(mf_tx_has_fee(&tx) == true);
    assert(mf_tx_is_fee_child(&tx) == false);

    char err[256] = {0};
    int rc = mf_tx_rule_validate(&tx, err, sizeof(err));
    assert(rc == 0);

    /* 测试手续费子单构造不变式 */
    mf_transaction_t fee_child = {0};
    rc = mf_tx_rule_build_fee_child(&tx, &fee_child);
    assert(rc == 0);
    assert(fee_child.parent_tx_id == 100);
    assert(fee_child.user_id == 1);
    assert(fee_child.account_id == 3);
    assert(strcmp(fee_child.type, "fee") == 0);
    assert(strstr(fee_child.note, "fee") != NULL);
    assert(mf_tx_is_fee_child(&fee_child) == true);

    printf("PASS: test_transaction_entity_and_rules\n");
}

int main(void) {
    test_transaction_entity_and_rules();
    return 0;
}
```

- [ ] **Step 2: 编写 Domain 实体头文件与值对象**

在 `backend/src/domain/transaction/entity.h` 中实现纯 C 实体定义：

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
    int64_t         asset_id;
    int64_t         account_id;
    int64_t         parent_tx_id;
    char            type[32];
    mf_quantity_t   amount;
    mf_price_t      price;
    mf_money_t      fee;
    char            fee_currency[8];
    char            note[256];
    char            tx_time[64];
    char            created_at[64];
    char            updated_at[64];
} mf_transaction_t;

static inline bool mf_tx_is_investment(const mf_transaction_t* tx) {
    if (!tx) return false;
    return (tx->asset_id > 0);
}

static inline bool mf_tx_is_fee_child(const mf_transaction_t* tx) {
    if (!tx) return false;
    return (tx->parent_tx_id > 0);
}

static inline bool mf_tx_has_fee(const mf_transaction_t* tx) {
    if (!tx) return false;
    return (tx->fee.amount > 0);
}
```

- [ ] **Step 3: 编写纯 C 仓储契约与领域业务规则**

在 `backend/src/domain/transaction/repository.h`、`rules.h` 与 `rules.c` 中实现规则校验与子单构建：

`domain/transaction/repository.h`:
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

`domain/transaction/rules.h`:
```c
#pragma once
#include <stddef.h>
#include "domain/transaction/entity.h"

int mf_tx_rule_validate(const mf_transaction_t* tx, char* err_buf, size_t err_len);
int mf_tx_rule_build_fee_child(const mf_transaction_t* parent, mf_transaction_t* out_fee);
```

`domain/transaction/rules.c`:
```c
#include "domain/transaction/rules.h"
#include <stdio.h>
#include <string.h>

int mf_tx_rule_validate(const mf_transaction_t* tx, char* err_buf, size_t err_len) {
    if (!tx) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Transaction entity is NULL");
        return -1;
    }
    if (tx->user_id <= 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Invalid user_id");
        return -1;
    }
    if (tx->type[0] == '\0') {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Transaction type cannot be empty");
        return -1;
    }
    if (strcmp(tx->type, "buy") == 0 || strcmp(tx->type, "sell") == 0) {
        if (tx->asset_id <= 0) {
            if (err_buf && err_len) snprintf(err_buf, err_len, "Investment transaction requires asset_id");
            return -1;
        }
        if (tx->price.amount <= 0) {
            if (err_buf && err_len) snprintf(err_buf, err_len, "Investment transaction requires positive price");
            return -1;
        }
    }
    if (tx->amount.raw < 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Transaction amount cannot be negative");
        return -1;
    }
    if (tx->fee.amount < 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Transaction fee cannot be negative");
        return -1;
    }
    return 0;
}

int mf_tx_rule_build_fee_child(const mf_transaction_t* parent, mf_transaction_t* out_fee) {
    if (!parent || !out_fee) return -1;
    if (parent->fee.amount <= 0) return -1;

    memset(out_fee, 0, sizeof(*out_fee));
    out_fee->user_id = parent->user_id;
    out_fee->asset_id = 0;
    out_fee->account_id = parent->account_id;
    out_fee->parent_tx_id = parent->id;
    snprintf(out_fee->type, sizeof(out_fee->type), "fee");
    out_fee->amount = mf_quantity_from_double(1.0);
    out_fee->price = mf_price_from_money(parent->fee);
    out_fee->fee = mf_money_zero();
    snprintf(out_fee->fee_currency, sizeof(out_fee->fee_currency), "%s", parent->fee_currency);

    if (parent->note[0]) {
        if (strstr(parent->note, "fee") != NULL) {
            snprintf(out_fee->note, sizeof(out_fee->note), "%s", parent->note);
        } else {
            snprintf(out_fee->note, sizeof(out_fee->note), "%s fee", parent->note);
        }
    } else {
        snprintf(out_fee->note, sizeof(out_fee->note), "fee");
    }

    if (parent->tx_time[0]) {
        snprintf(out_fee->tx_time, sizeof(out_fee->tx_time), "%s", parent->tx_time);
    }
    return 0;
}
```

- [ ] **Step 4: 配置 CMake 并运行 Domain 单元测试**

在 `backend/CMakeLists.txt` 中注册 `test_domain_transaction` 并执行：
```bash
cmake -B backend/build -G "Unix Makefiles"
cmake --build backend/build --target test_domain_transaction
./backend/build/tests/test_domain_transaction
```
Expected: `PASS: test_transaction_entity_and_rules`

- [ ] **Step 5: 提交 Domain 层基础建设**

```bash
git add backend/src/domain/ backend/tests/unit/test_domain_transaction.c backend/CMakeLists.txt
git commit -m "feat(domain): ✨ implement pure domain transaction entities and business rules"
```

---

### Task 2: 建立 Application 应用编排层 (`application/transaction/`)

**Files:**
- Create: `backend/src/application/transaction/commands.h`
- Create: `backend/src/application/transaction/dtos.h`
- Create: `backend/src/application/transaction/usecases.h`
- Create: `backend/src/application/transaction/usecases.c`

- [ ] **Step 1: 定义 Application 命令与 DTO**

`backend/src/application/transaction/commands.h`:
```c
#pragma once
#include <stdint.h>

typedef struct create_tx_cmd {
    int64_t     user_id;
    int64_t     asset_id;
    int64_t     account_id;
    const char* type;
    double      amount;
    double      price;
    double      fee;
    const char* fee_currency;
    const char* note;
    const char* tx_time;
} create_tx_cmd_t;

typedef struct delete_tx_cmd {
    int64_t     user_id;
    int64_t     tx_id;
} delete_tx_cmd_t;

typedef struct query_tx_filter {
    int64_t     user_id;
    int64_t     asset_id;
    const char* type;
    const char* start_date;
    const char* end_date;
    int64_t     page;
    int64_t     page_size;
} query_tx_filter_t;
```

`backend/src/application/transaction/dtos.h`:
```c
#pragma once
#include <stdint.h>
#include "domain/transaction/entity.h"

typedef struct tx_usecase_result {
    int         code;
    char        message[256];
    int64_t     created_id;
    void*       data_payload;
    int64_t     total;
} tx_usecase_result_t;
```

- [ ] **Step 2: 编写用例编排与事务管控实现**

在 `backend/src/application/transaction/usecases.h` 和 `usecases.c` 中编排：
- `tx_usecase_create`：校验规则 -> `BEGIN TRANSACTION` -> `mf_tx_repo_save` -> `apply_position` / 账本引擎 -> `balance_apply_delta` -> 处理手续费子单 -> `COMMIT`。
- `tx_usecase_delete`：`BEGIN TRANSACTION` -> `mf_tx_repo_find_by_id` -> 回滚手续费子单 -> 删除手续费子单 -> 回滚持仓与资金余额 -> 删除主交易 -> `COMMIT`。
- 确认该文件中 **零 SQL 语句**。

- [ ] **Step 3: 静态检查验证 Application 零 SQL**

```bash
grep -E "(SELECT|INSERT|UPDATE|DELETE|FROM|WHERE)" backend/src/application/transaction/* || echo "Zero SQL in Application"
```
Expected: 输出 `Zero SQL in Application`

- [ ] **Step 4: 提交 Application 层用例编排**

```bash
git add backend/src/application/
git commit -m "feat(application): ✨ implement transaction use cases with transaction boundaries and orchestration"
```

---

### Task 3: 建立 Infrastructure 仓储具体实现 (`infrastructure/repositories/`)

**Files:**
- Create: `backend/src/infrastructure/repositories/transaction_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/transaction_repo_impl.c`

- [ ] **Step 1: 编写符合纯 C 契约的参数化 SQL 仓储实现**

在 `backend/src/infrastructure/repositories/transaction_repo_impl.c` 中：
- 实现 `mf_tx_repo_find_by_id`：执行 `SELECT ... FROM transactions WHERE user_id = ? AND id = ?`，将字段映射为 `mf_transaction_t`；
- 实现 `mf_tx_repo_save`：执行 `INSERT INTO transactions (...) VALUES (...)`，支持 `parent_tx_id`；
- 实现 `mf_tx_repo_update` 与 `mf_tx_repo_delete`；
- 实现 `mf_tx_repo_find_fee_children` 与 `mf_tx_repo_delete_fee_children`。
- 确保**不包含任何手续费计算、资产正负向逻辑**（纯粹的数据访问与实体映射）。

- [ ] **Step 2: 编译验证基础设施层**

```bash
cmake --build backend/build --parallel
```
Expected: 编译通过 0 错误。

- [ ] **Step 3: 提交 Infrastructure 仓储实现**

```bash
git add backend/src/infrastructure/
git commit -m "feat(infra): ✨ implement infrastructure transaction repository conforming to domain contract"
```

---

### Task 4: 建立 Interfaces 控制器与 Services 兼容门面

**Files:**
- Create: `backend/src/interfaces/http/controllers/transaction_controller.h`
- Create: `backend/src/interfaces/http/controllers/transaction_controller.c`
- Modify: `backend/src/services/transaction_service.c`
- Modify: `backend/src/services/transaction_write.c`
- Modify: `backend/src/services/transaction_query.c`
- Modify: `backend/CMakeLists.txt`

- [ ] **Step 1: 编写极薄 Interfaces HTTP 控制器**

在 `backend/src/interfaces/http/controllers/transaction_controller.c` 中：
- 解析 HTTP 请求参数；
- 转换为 `create_tx_cmd_t` / `delete_tx_cmd_t`；
- 调用 `tx_usecase_create` / `tx_usecase_delete`；
- 封装 `respond_ok` / `respond_error`；
- 提供 `register_transaction_routes(app)`。

- [ ] **Step 2: 更新 `services/` 门面委派至新用例**

在 `backend/src/services/transaction_write.c` 与 `transaction_service.c` 中：
- 将内部实现转调为 `tx_usecase_create` 与 `tx_usecase_delete`；
- 保留旧函数签名，使所有现有调用与测试无感平滑运行。

- [ ] **Step 3: 提交 Interfaces 与门面适配**

```bash
git add backend/src/interfaces/ backend/src/services/ backend/CMakeLists.txt
git commit -m "refactor(interfaces): ♻️ implement HTTP transaction controller and service facades"
```

---

### Task 5: 严谨验收标准与全量自动化回归验证

**Files:**
- Test: `backend/build/` CTest 单元测试矩阵 (14 Suites)
- Test: `backend/tests/test_link.sh` (38 Cases, 139 Assertions)
- Test: `backend/tests/test_ledgers.sh`
- Test: `backend/tests/test_2fa.sh`
- Test: `backend/tests/test_dca_cashflow.sh`
- Test: `backend/tests/test_ai_trace.sh`
- Test: `backend/tests/test_market_sync.sh`
- Test: `backend/tests/test_fx_oauth.sh`
- Test: Frontend `npm test && npm run build`

- [ ] **Step 1: 运行全量 14 项 CTest 单元测试套件**

```bash
cd backend/build && ctest --output-on-failure && cd ../..
```
Expected: 100% tests passed out of 14.

- [ ] **Step 2: 运行全量 7 大集成测试套件**

```bash
./backend/tests/test_link.sh && \
./backend/tests/test_ledgers.sh && \
./backend/tests/test_2fa.sh && \
./backend/tests/test_dca_cashflow.sh && \
./backend/tests/test_ai_trace.sh && \
./backend/tests/test_market_sync.sh && \
./backend/tests/test_fx_oauth.sh
```
Expected: 所有断言 100% PASS。

- [ ] **Step 3: 运行前端回归测试与编译**

```bash
npm --prefix frontend test && npm --prefix frontend run build
```
Expected: 0 errors.

- [ ] **Step 4: 提交全量验证**

```bash
git commit --allow-empty -m "test(layering): ✅ verify domain application infrastructure layering with all unit and integration suites"
```
