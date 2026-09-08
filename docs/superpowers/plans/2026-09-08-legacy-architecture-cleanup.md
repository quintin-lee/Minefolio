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
| **4** | import/export | 🟢 低 | 0.5 天 | ✅ 完成 |
| **5** | 清理 | 🟢 低 | 0.5 天 | ⏳ 进行中 |

**总进度**: 9/9 域完成 (100%) ✅

## ✅ 已完成的域

### 1-9. 所有域 DDD 迁移已完成

详见之前的迁移记录。每个域都完成了：
- Domain 层：实体、仓储契约、业务规则
- Application 层：用例编排
- Infrastructure 层：仓储 SQL 实现
- Interface 层：控制器调用 usecase

## ⏳ Phase 5: 清理旧代码 (进行中)

### 依赖分析结果

经过详细分析，发现以下文件仍被其他组件引用：

#### 仍被引用的 Legacy Repository 文件

| 文件 | 引用方 | 原因 |
|------|--------|------|
| `repositories/daily_expense_repo.c/h` | AI tools (cashflow, expense, portfolio, transaction), workflows (cashflow_forecast, monthly_review), infrastructure impl | AI 工具直接调用 SQL 函数 |
| `repositories/category_repo.c/h` | AI tools (cashflow, expense) | AI 工具直接调用 SQL 函数 |
| `repositories/transfer_repo.c/h` | AI tools (transfer) | AI 工具直接调用 SQL 函数 |
| `repositories/dca_repo.c/h` | market_scheduler | 定时任务直接调用 SQL 函数 |
| `repositories/ledger_repo.c/h` | common/ctx.h, application/auth | JWT 上下文和认证使用 |
| `repositories/asset_repo.c/h` | AI tools (asset, cashflow, expense, portfolio, transfer), workflows, infrastructure impl, application/usecases | 广泛使用 |
| `repositories/auth_repo.c/h` | infrastructure/auth_repo_impl, admin_controller | 认证基础设施 |
| `repositories/cashflow_repo.c/h` | application/cashflow/usecases, infrastructure impl | 现金流用例 |
| `repositories/tag_repo.c/h` | application/tag/usecases | Tag 用例 (可迁移到 infra impl) |
| `repositories/transaction_repo.c/h` | unit tests, common/balance | 单元测试和余额计算 |

#### 仍被引用的 Legacy Service 文件

| 文件 | 引用方 | 原因 |
|------|--------|------|
| `services/report_*_service.c/h` | report usecases | 报表域跨域依赖 |
| `services/import_service.c/h` | import/export usecases | 导入逻辑复杂 |
| `services/export_service.c/h` | import/export usecases | 导出逻辑复杂 |

### 当前状态

由于 AI tools 和基础设施层仍大量依赖 legacy repository 文件，**无法安全删除任何 legacy 文件**。

### 未来清理计划

#### Phase 6: AI Tools 迁移 (优先级: 高)

AI 工具直接调用 legacy repository 函数，需要单独迁移：

| AI Tool | 依赖的 Legacy Repo | 迁移难度 |
|---------|-------------------|----------|
| `cashflow_tool.c` | daily_expense_repo, category_repo, asset_repo | 🟡 中 |
| `expense_tool.c` | daily_expense_repo, category_repo, asset_repo | 🟡 中 |
| `portfolio_tool.c` | daily_expense_repo, asset_repo | 🟢 低 |
| `transaction_tool.c` | daily_expense_repo | 🟢 低 |
| `transfer_tool.c` | transfer_repo, asset_repo | 🟢 低 |
| `asset_tool.c` | asset_repo | 🟢 低 |
| `cashflow_forecast.c` | daily_expense_repo, asset_repo | 🟢 低 |
| `monthly_review.c` | daily_expense_repo, asset_repo | 🟢 低 |
| `financial_health.c` | asset_repo | 🟢 低 |
| `portfolio_analysis.c` | asset_repo | 🟢 低 |

**迁移策略**：
1. 为 AI tools 创建专用的仓储接口 (`domain/ai/repository.h`)
2. 创建基础设施实现 (`infrastructure/repositories/ai_repo_impl.c`)
3. AI tools 改为调用新的仓储接口

#### Phase 7: Infrastructure Impl 清理 (优先级: 中)

将 infrastructure 实现中的 legacy header 引用替换为新的仓储接口：

| 文件 | 当前引用 | 替换为 |
|------|----------|--------|
| `asset_repo_impl.c` | `repositories/asset_repo.h` | 直接实现，无需引用 |
| `cashflow_repo_impl.c` | `repositories/cashflow_repo.h` | 直接实现，无需引用 |
| `auth_repo_impl.c` | `repositories/auth_repo.h` | 直接实现，无需引用 |
| `tag_repo_impl.c` | `repositories/tag_repo.h` | 直接实现，无需引用 |

#### Phase 8: Unit Test 清理 (优先级: 低)

将单元测试中的 legacy repo 引用替换为新的仓储接口：

| 测试文件 | 当前引用 | 替换为 |
|----------|----------|--------|
| `test_domain_transaction.c` | `repositories/transaction_repo.c` | 直接测试 domain rules |

#### Phase 9: 删除 Legacy Files (优先级: 低)

在完成 Phase 6-8 后，可以安全删除以下文件：

| 文件 | 删除条件 |
|------|----------|
| `repositories/tag_repo.c/h` | Phase 7 完成 |
| `repositories/category_repo.c/h` | Phase 6 完成 |
| `repositories/ledger_repo.c/h` | Phase 8 完成 |
| `repositories/transfer_repo.c/h` | Phase 6 完成 |
| `repositories/dca_repo.c/h` | Phase 7 完成 |
| `repositories/daily_expense_repo.c/h` | Phase 6 完成 |
| `repositories/asset_repo.c/h` | Phase 6+7 完成 |
| `repositories/auth_repo.c/h` | Phase 7 完成 |
| `repositories/cashflow_repo.c/h` | Phase 7 完成 |
| `repositories/transaction_repo.c/h` | Phase 8 完成 |

## 📝 文档更新

迁移完成后，需更新：
- `docs/architecture.md` — 更新架构图和目录说明
- `AGENTS.md` — 更新 Key Directories 表格
- `CHANGELOG.md` — 记录架构迁移

## ⚠️ 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| AI tools 依赖 legacy repos | 无法完全删除旧代码 | Phase 6 单独迁移 |
| 跨域依赖复杂 | 报表/导入导出域无法完全拆分 | 用例层隔离，保留 service 作为基础设施 |
| 前端测试缺失 | UI 回归风险 | 集成测试覆盖核心 API |
| 性能回归 | 报表查询变慢 | 性能基准测试，保留原 SQL |
| 事务一致性 | 多域操作原子性 | 用例层管理事务边界 |

## 🎯 成功标准

- [x] 所有域完成 DDD 四层架构迁移
- [ ] Phase 6: AI Tools 迁移到新架构
- [ ] Phase 7: Infrastructure Impl 清理
- [ ] Phase 8: Unit Test 清理
- [ ] Phase 9: 删除所有 legacy files
- [ ] 文档更新
- [ ] 零回归测试
- [ ] 性能无退化
