# 边界收敛架构重构实施计划 (Boundary Convergence Refactoring Plan)

## 概述与目标

针对 Minefolio 后端 DDD 四层架构存在的边界模糊与逆向依赖问题进行系统性收敛重构：
1. **统一 Domain 仓储契约**：将 `domain/account/repository.h` 中的 `csilk_db_pool_t* pool` 修正为全系统统一的 `void* pool`，彻底解耦 Domain 对基础设施连接池类型的依赖。
2. **下沉 Application 裸 SQL**：将 `application/` 目录下的 24 处裸 SQL 查询（涉及 ai, asset, cashflow, dca, scheduler, transaction）全部下沉至 `infrastructure/repositories/`，保持用例层纯净的业务编排。
3. **断开基础设施逆向依赖**：解除 `portfolio_repo_impl` 和 `file_repo_impl` 对 `services/` 的反向 `#include`，恢复严格单向依赖（Application/Service → Infrastructure，禁止反向）。
4. **校准架构规范文档**：同步更新 `AGENTS.md`，精确规范 SQL 归属、分层隔离契约与用例层职责。

---

## 任务拆解与实施步骤

### Task 1: Domain 仓储契约参数统一改为 `void* pool`
- **目标**：消除 `domain/account/repository.h` 中唯一的 `csilk_db_pool_t*` 基础设施类型泄漏。
- **涉及文件**：
  - `backend/src/domain/account/repository.h`
  - `backend/src/infrastructure/repositories/account_repo_impl.h`
  - `backend/src/infrastructure/repositories/account_repo_impl.c`
  - `backend/src/application/account/usecases.c`
- **步骤**：
  1. 将 `domain/account/repository.h` 与 `account_repo_impl.h` 中 `account_*` 接口的 `csilk_db_pool_t* pool` 统一改为 `void* pool`。
  2. 在 `account_repo_impl.c` 中显式转换为 `csilk_db_pool_t* db_pool = (csilk_db_pool_t*)pool;`。
  3. 在 `application/account/usecases.c` 中直接传递 `pool`，无需冗余强转。
  4. 编译与单元测试验证：运行 `./backend/tests/run_isolated.sh ctest -R test_account`。

---

### Task 2: 断开 `file_repo_impl` 对 `services/file_parser.h` 的反向依赖
- **目标**：文件解析是服务层/无状态处理逻辑，不属于数据库持久化。
- **涉及文件**：
  - `backend/src/infrastructure/repositories/file_repo_impl.h`
  - `backend/src/infrastructure/repositories/file_repo_impl.c`
  - `backend/src/domain/file/repository.h`
  - `backend/src/application/file/usecases.c`
- **步骤**：
  1. `application/file/usecases.c` 直接 `#include "services/file_parser.h"` 并在 `mf_file_usecase_parse` 中调用 `file_parse`。
  2. 从 `infrastructure/repositories/file_repo_impl.c` 和 `domain/file/repository.h` 中移除 `mf_file_repo_parse`。
  3. 移除 `file_repo_impl.c` 中对 `services/file_parser.h` 的 `#include`。
  4. 编译并验证全套测试通过。

---

### Task 3: 断开 `portfolio_repo_impl` 对 `services/market/exchange_rate_service.h` 的反向依赖
- **目标**：仓储层只负责拉取数据库原始持仓/交易数据，币种汇率折算与金额聚合属于用例/服务层职责。
- **涉及文件**：
  - `backend/src/infrastructure/repositories/portfolio_repo_impl.h`
  - `backend/src/infrastructure/repositories/portfolio_repo_impl.c`
  - `backend/src/application/portfolio/usecases.c`
- **步骤**：
  1. 在 `portfolio_repo_impl.c` 中剥离依赖 `exchange_rate_service.h` 的三个逻辑：
     - `portfolio_repo_get_total_liabilities_m`：拆分为仓储查询原始负债资产 `portfolio_repo_get_liabilities_raw`，汇率换算与 `money_add` 循环移至 `application/portfolio/usecases.c`。
     - `portfolio_repo_get_current_holdings_totals_m`：拆分为仓储查询持仓基础行 `portfolio_repo_get_holdings_raw`，换算累加移至 `usecases.c`。
     - `portfolio_repo_get_trend_30d`：仓储负责查询资产列表及区间内 balance_logs；历史汇率查询与折算聚合移至 `usecases.c`。
  2. 移除 `portfolio_repo_impl.c` 中的 `#include "services/market/exchange_rate_service.h"`。
  3. 编译验证并运行 `ctest -R portfolio`。

---

### Task 4: 下沉 Application 层 24 处裸 SQL 到对应 Repo
- **4.1 AI 域 (1 处)**:
  - `backend/src/application/ai/usecases.c` (line 246): `DELETE FROM ai_traces WHERE session_id=? AND user_id=?`
  - 下沉至 `infrastructure/repositories/ai_trace_repo_impl.c`: `int mf_ai_trace_repo_delete_by_session(void* pool, int64_t user_id, int64_t session_id)`。
- **4.2 Asset 域 (3 处)**:
  - `backend/src/application/asset/usecases.c` (line 247): `SELECT COUNT(*) as cnt FROM transactions WHERE user_id=? AND asset_id=?`
    - 下沉至 `asset_repo_impl.c`: `int asset_count_transactions(void* pool, int64_t user_id, int64_t asset_id)`。
  - `backend/src/application/asset/usecases.c` (line 288): `SELECT ledger_id FROM assets WHERE id=? AND user_id=?`
    - 下沉至 `asset_repo_impl.c`: `int64_t asset_get_ledger_id(void* pool, int64_t user_id, int64_t asset_id)`。
  - `backend/src/application/asset/usecases.c` (line 688): `SELECT a.current_value, a.currency, c.asset_type FROM assets a JOIN categories c ...`
    - 下沉至 `asset_repo_impl.c`: `csilk_json_t* asset_list_values_with_type(void* pool, int64_t user_id, int64_t ledger_id)`。
- **4.3 Cashflow 域 (3 处) 与 DCA 域 (1 处)**:
  - `backend/src/application/cashflow/usecases.c` (line 35 & line 74): `cashflow_plans` 多表连接全量/单条查询
    - 下沉至 `cashflow_repo_impl.c`: `csilk_json_t* cashflow_repo_list_detailed(void* pool, int64_t user_id)` 与 `cashflow_repo_get_detailed(void* pool, int64_t user_id, int64_t id)`。
  - `backend/src/application/cashflow/usecases.c` (line 423) & `backend/src/application/dca/usecases.c` (line 227): `SELECT currency FROM assets WHERE id=?`
    - 下沉至 `asset_repo_impl.c`: `int asset_get_currency(void* pool, int64_t asset_id, char* out_currency, size_t max_len)`。
- **4.4 Scheduler 域 (3 处)**:
  - `backend/src/application/scheduler/usecases.c`:
    - line 47: 活跃作业数统计 `SELECT COUNT(*) as cnt FROM scheduled_jobs ...`
    - line 61: 今日运行次数统计 `SELECT COUNT(*) as cnt FROM job_run_logs ...`
    - line 75: 下次待运行作业 `SELECT name, next_run_at FROM scheduled_jobs ... LIMIT 1`
    - 下沉至 `scheduler_repo_impl.c`: `int scheduler_repo_count_active(...)`, `int scheduler_repo_count_runs_today(...)`, `csilk_json_t* scheduler_repo_get_next_run(...)`。
- **4.5 Transaction 域 (13 处)**:
  - `backend/src/application/transaction/usecases.c`:
    - line 183 & line 391: `UPDATE transactions SET fx_rate_to_cny = ? ...`
      - 下沉至 `transaction_repo_impl.c`: `int tx_update_fx_rate(void* pool, int64_t user_id, int64_t tx_id, double fx_rate)`。
    - line 465: `SELECT id, transaction_type, parent_tx_id, asset_id ... FROM transactions ...`
      - 下沉至 `transaction_repo_impl.c`: `csilk_json_t* tx_get_raw(void* pool, int64_t user_id, int64_t id)`。
    - lines 542, 548, 561, 566, 596, 620, 626, 640, 655 (转账级联与对账清理相关 SQL)
      - 下沉至 `transaction_repo_impl.c` 与 `transfer_repo_impl.c`：封装专用清理与查找函数。

---

### Task 5: 同步修订 `AGENTS.md` 架构准则
- **目标**：精确描述分层职责与 SQL 分布，避免过强、不符合实际的绝对化断言。
- **涉及文件**：`AGENTS.md`
- **步骤**：
  1. 将 "All SQL now lives in infrastructure/repositories/*_repo_impl.c" 修订为：
     - 领域业务持久化 SQL 严格收敛在 `infrastructure/repositories/*_repo_impl.c` 中。
     - 用例层（`application/`）纯粹进行业务流程与事务编排，禁止编写裸 SQL 字符串。
     - 核心账本重放引擎（`core/ledger/`）及通用账本工具（`common/ledger_utils.c`）保留各自底层算法专用的轻量 SQL。
     - 基础设施层（`infrastructure/repositories/`）严格禁止反向包含 `services/` 或 `application/`。

---

### Task 6: 隔离构建与双引擎全量回归测试
- **步骤**：
  1. 编译构建：`./backend/tests/run_isolated.sh cmake --build ./backend/build --parallel`
  2. 运行 CTest 80 套件全量单元测试：`./backend/tests/run_isolated.sh ctest --test-dir ./backend/build --output-on-failure`
  3. 运行核心集成测试：`./backend/tests/run_isolated.sh ./backend/tests/test_link.sh`
  4. 运行 IM 绑定集成测试：`./backend/tests/run_isolated.sh ./backend/tests/test_im_bind_auth.sh`
  5. 前端编译验证：`npm --prefix frontend run build`
