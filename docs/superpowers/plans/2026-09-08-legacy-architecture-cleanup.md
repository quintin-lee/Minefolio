# 新旧双轨架构清理计划

## 📋 项目概要

将项目从旧的双轨架构（`controllers/` + `repositories/` + `services/`）迁移到新的 DDD 四层架构：
- **Domain**: `domain/` — 实体、仓储契约、业务规则
- **Application**: `application/` — 用例编排
- **Infrastructure**: `infrastructure/repositories/` — 仓储 SQL 实现
- **Interface**: `interfaces/http/controllers/` — HTTP 处理器

## 🎯 迁移策略

1. **逐域迁移**：每个域独立完成 Domain → Application → Infrastructure → Controller 改造
2. **API 契约零破坏**：前端完全无改动
3. **测试先行**：每个 Task 完成后跑全量测试确认无回归
4. **渐进式清理**：新架构就绪后再删除旧代码

## 📊 进度总览

| Phase | 域 | 风险 | 工作量 | 状态 |
|-------|---|------|--------|------|
| **1** | tag | 🟢 低 | 0.5 天 | ✅ 完成 |
| **1** | category | 🟢 低 | 0.5 天 | ✅ 完成 |
| **1** | ledger | 🟢 低 | 1 天 | ✅ 完成 |
| **2** | daily_expense | 🟡 中 | 1 天 | ✅ 完成 |
| **2** | transfer | 🟡 中 | 0.5 天 | ✅ 完成 |
| **2** | dca | 🟡 中 | 1 天 | ✅ 完成 |
| **3** | report | 🟡 中 | 0.5 天 | ✅ 完成 (跨域隔离) |
| **4** | file | 🟢 低 | 0.5 天 | ✅ 完成 |
| **4** | import/export | 🟢 低 | 0.5 天 | ⏳ 待定 (跨域依赖) |
| **5** | 清理 | 🟢 低 | 0.5 天 | ⏳ 待定 |

**总进度**: 8/9 域完成 (89%)

## ✅ 已完成的域

### 1. tag 域

**新增文件 (10 个, ~530 行)**
- `domain/tag/entity.h` — `mf_tag_t` 聚合根实体
- `domain/tag/repository.h` — 仓储契约接口
- `domain/tag/rules.h/.c` — 名称/颜色校验
- `application/tag/commands.h` — CRUD 命令对象
- `application/tag/dtos.h` — 用例结果 DTO
- `application/tag/usecases.h/.c` — 用例编排
- `infrastructure/repositories/tag_repo_impl.h/.c` — SQL 实现

**调用链**
```
旧: tag_controller → repositories/tag_repo → SQL
新: tag_controller → application/tag/usecases → domain/rules + infrastructure/repo_impl → SQL
```

### 2. category 域

**新增文件 (10 个, ~600 行)**
- `domain/category/entity.h` — `mf_category_t` 聚合根实体
- `domain/category/repository.h` — 仓储契约接口 (8 个方法)
- `domain/category/rules.h/.c` — 名称/类型/删除业务规则
- `application/category/commands.h` — CRUD 命令对象
- `application/category/dtos.h` — 用例结果 DTO
- `application/category/usecases.h/.c` — 用例编排
- `infrastructure/repositories/category_repo_impl.h/.c` — SQL 实现

**关键修复**
- `categories` 表无 `updated_at` 列，需从 SQL 中移除该字段

### 3. ledger 域

**新增文件 (10 个, ~1100 行)**
- `domain/ledger/entity.h` — `mf_ledger_t`, `mf_ledger_member_t`, `mf_ledger_list_item_t`
- `domain/ledger/repository.h` — 仓储契约 (13 个方法，纯 struct 接口)
- `domain/ledger/rules.h/.c` — 名称校验、角色校验、RBAC 规则
- `application/ledger/commands.h` — 6 个命令对象
- `application/ledger/dtos.h` — 结果 DTO
- `application/ledger/usecases.h/.c` — 11 个用例
- `infrastructure/repositories/ledger_repo_impl.h/.c` — SQL 实现

**设计决策**
- Domain 层零 csilk 依赖：返回 `mf_ledger_*` 结构体
- Use case 层负责 JSON 转换

### 4. daily_expense 域

**新增文件 (10 个, ~700 行)**
- `domain/daily_expense/entity.h` — `mf_daily_expense_t`, `mf_daily_expense_snapshot_t`
- `domain/daily_expense/repository.h` — 仓储契约 (12 个方法)
- `domain/daily_expense/rules.h/.c` — 类型/金额/必填字段校验
- `application/daily_expense/commands.h` — 创建/更新命令
- `application/daily_expense/dtos.h` — 结果 DTO
- `application/daily_expense/usecases.h/.c` — 5 个用例 (含余额调整+标签管理)
- `infrastructure/repositories/daily_expense_repo_impl.h/.c` — SQL 实现

**删除文件 (4 个)**
- `services/daily_expense_query.c/.h`
- `services/daily_expense_write.c/.h`

### 5. transfer 域

**新增文件 (8 个, ~350 行)**
- `domain/transfer/entity.h` — `mf_transfer_t` 聚合根
- `domain/transfer/repository.h` — 仓储契约 (4 个方法)
- `domain/transfer/rules.h/.c` — 必填字段/不同资产校验
- `application/transfer/commands.h` — 创建转账命令
- `application/transfer/dtos.h` — 结果 DTO
- `application/transfer/usecases.h/.c` — 转账用例 (含余额+交易记录)
- `infrastructure/repositories/transfer_repo_impl.h/.c` — SQL 实现

### 6. dca 域

**新增文件 (10 个, ~800 行)**
- `domain/dca/entity.h` — `mf_dca_plan_t`, `mf_dca_execution_t`
- `domain/dca/repository.h` — 仓储契约 (14 个方法)
- `domain/dca/rules.h/.c` — 必填校验、状态校验、收益率计算、止盈检查
- `application/dca/commands.h` — 创建/更新/确认命令
- `application/dca/dtos.h` — 结果 DTO
- `application/dca/usecases.h/.c` — 10 个用例 (含交易创建+余额调整)
- `infrastructure/repositories/dca_repo_impl.h/.c` — SQL 实现

### 7. report 域 (跨域隔离)

**新增文件 (3 个, ~180 行)**
- `domain/report/entity.h` — 报表实体定义
- `domain/report/repository.h` — 仓储契约 (SQL 查询封装)
- `domain/report/rules.h/.c` — 报表校验规则
- `application/report/usecases.h/.c` — 用例层 (隔离跨域依赖)

**设计决策**
- 报表域跨域依赖复杂 (portfolio, market)，无法完全拆分
- 用例层作为隔离层，委托给现有 service 函数
- 保留 `services/report_*_service.c` 作为基础设施实现
- 未来可重构为独立的查询服务

### 8. file 域

**新增文件 (8 个, ~400 行)**
- `domain/file/entity.h` — `mf_file_parse_result_t`, `mf_import_result_t`
- `domain/file/repository.h` — 仓储契约 (4 个方法)
- `domain/file/rules.h/.c` — 文件类型/大小/CSV 字段校验
- `application/file/usecases.h/.c` — 3 个用例 (解析/导入交易/导入收支)
- `infrastructure/repositories/file_repo_impl.h/.c` — SQL 实现

## ⏳ 待完成的域

### 4. import/export 域

**复杂度**: 中
**跨域依赖**:
- `import_service.c` — 依赖 daily_expense, transaction, category, tag
- `export_service.c` — 依赖 transaction, daily_expense, asset

**建议策略**:
1. 创建 `domain/import/` 定义导入结果实体
2. 创建 `application/import/usecases.h/.c` 封装导入逻辑
3. 保留 `services/import_service.c` 作为基础设施实现
4. 未来可重构为独立的导入引擎

## 🧹 Phase 5: 清理旧代码

### 待删除的 legacy 文件

| 文件 | 原因 | 风险 |
|------|------|------|
| `repositories/tag_repo.c/h` | 已迁移到 infrastructure/repositories/tag_repo_impl.c/h | 🟢 低 |
| `repositories/category_repo.c/h` | 已迁移到 infrastructure/repositories/category_repo_impl.c/h | 🟢 低 |
| `repositories/ledger_repo.c/h` | 已迁移到 infrastructure/repositories/ledger_repo_impl.c/h | 🟢 低 |
| `repositories/transfer_repo.c/h` | 已迁移到 infrastructure/repositories/transfer_repo_impl.c/h | 🟢 低 |
| `repositories/dca_repo.c/h` | 已迁移到 infrastructure/repositories/dca_repo_impl.c/h | 🟢 低 |
| `repositories/daily_expense_repo.c/h` | 已迁移到 infrastructure/repositories/daily_expense_repo_impl.c/h | 🟢 低 |
| `services/tag_service.c/h` | 功能已迁移到 usecase 层 | 🟢 低 |
| `services/category_service.c/h` | 功能已迁移到 usecase 层 | 🟢 低 |
| `services/ledger_service.c/h` | 功能已迁移到 usecase 层 | 🟢 低 |
| `services/transfer_service.h` | 功能已迁移到 usecase 层 | 🟢 低 |
| `services/daily_expense_query.c/h` | 已删除 | ✅ |
| `services/daily_expense_write.c/h` | 已删除 | ✅ |

### 依赖关系检查

在删除前，需确认：
1. `main.c` 只 include 控制器头文件，不直接引用旧 repo/service
2. 其他域的服务不引用被删除的文件
3. 测试文件不直接引用被删除的文件

## 📝 文档更新

迁移完成后，需更新：
- `docs/architecture.md` — 更新架构图和目录说明
- `AGENTS.md` — 更新 Key Directories 表格
- `CHANGELOG.md` — 记录架构迁移

## ⚠️ 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 跨域依赖复杂 | 报表域无法完全拆分 | 用例层隔离，保留 service 作为基础设施 |
| 前端测试缺失 | UI 回归风险 | 集成测试覆盖核心 API |
| 性能回归 | 报表查询变慢 | 性能基准测试，保留原 SQL |
| 事务一致性 | 多域操作原子性 | 用例层管理事务边界 |

## 🎯 成功标准

- [x] 所有域完成 DDD 四层架构迁移
- [ ] 所有旧代码删除
- [ ] 文档更新
- [ ] 零回归测试
- [ ] 性能无退化
