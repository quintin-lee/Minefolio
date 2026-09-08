# Minefolio 新旧双轨架构清理计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完全移除 Legacy Controllers/Services 层，所有路由统一走 DDD 四层架构（Interfaces → Application → Domain → Infrastructure），消除双轨并存的技术债。

**Architecture:** 保持现有 DDD 四层架构不变，将所有"半迁移"控制器中的 Legacy Service/Repository 直接调用替换为 Application Use Case 调用，将"未迁移"控制器中的业务逻辑下沉到 Application/Domain 层，最终删除旧 `controllers/`、`services/`、`repositories/` 中已废弃的文件。

**Tech Stack:** C23, csilk v0.5.2, DDD Four-Layer Architecture

---

## 迁移状态总览

| 域 | Interfaces | Application | Domain | Infrastructure | 状态 |
|----|:----------:|:-----------:|:------:|:--------------:|------|
| auth | ✅ | ✅ | ✅ | ✅ | **已完成** |
| asset | ✅ | ✅ | ✅ | ✅ | **已完成** |
| transaction | ✅ | ✅ | ✅ | ✅ | **已完成** |
| cashflow | ✅ | ✅ | ✅ | ✅ | **已完成** |
| daily_expense | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| transfer | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| dca | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| category | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| tag | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| ledger | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| file | ✅ | ✅ | ✅ | ✅ | **已完成** ✨ |
| market | ✅ | ✅ | ✅ | ✅ | **已完成** |
| report | ⚠️ 调用 legacy service | — | — | — | **保留** (跨域依赖) |
| import/export | ⚠️ 调用 legacy service | — | — | — | **保留** (跨域依赖) |
| import_rule | — | — | — | — | **保留** (被 import 依赖) |
| receipt | — | — | — | — | **未迁移** |
| admin | — | — | — | — | **未迁移** |

**迁移完成率：12/17 域 (71%)**

---

## 已完成的迁移

### Phase 1: 简单 CRUD 域 ✅

| Task | 域 | 状态 | Commit |
|------|---|------|--------|
| Task 1 | tag | ✅ 完成 | `ede4f0c8` |
| Task 2 | category | ✅ 完成 | `ede4f0c8` |
| Task 3 | ledger | ✅ 完成 | `3022965e` |

### Phase 2: 业务编排域 ✅

| Task | 域 | 状态 | Commit |
|------|---|------|--------|
| Task 4 | daily_expense | ✅ 完成 | `303c6132` |
| Task 5 | transfer | ✅ 完成 | `d6be6be3` |
| Task 6 | dca | ✅ 完成 | `bb7f00b5` |

### Phase 3: 报表查询域 ⚠️

| Task | 域 | 状态 | 原因 |
|------|---|------|------|
| Task 7 | report | ⚠️ 保留 | 跨域依赖复杂（portfolio, holdings, market），重构成本高 |

### Phase 4: 辅助域 ✅

| Task | 域 | 状态 | Commit |
|------|---|------|--------|
| Task 8 | file | ✅ 完成 | `da6663ee` |
| Task 8 | import/export | ⚠️ 保留 | 跨域依赖（import_rule, ledger_engine, balance） |
| Task 8 | import_rule | ⚠️ 保留 | 被 import_service 依赖 |
| Task 8 | receipt | ❌ 未开始 | — |

---

## 保留的 Legacy 文件

以下文件因跨域依赖暂时保留：

### Services（保留）
```
backend/src/services/
├── category_service.c/h        # 被 auth/admin 使用 (categories_seed_defaults)
├── report_expense_service.c/h  # 跨域依赖复杂
├── report_asset_service.c/h    # 跨域依赖复杂
├── report_holdings_service.c/h # 跨域依赖复杂
├── import_service.c/h          # 跨域依赖（import_rule, ledger_engine）
├── export_service.c/h          # 跨域依赖
├── file_parser.c/h             # 被 file_repo_impl 包装
├── ai_service.c/h              # AI Runtime 已在 services/ai/
├── ai_tools.c/h                # AI Runtime 已在 services/ai/
└── ai_workflow_service.c/h     # AI Runtime 已在 services/ai/
```

### Repositories（保留）
```
backend/src/repositories/
├── category_repo.c/h           # 被 category_repo_impl 包装
├── tag_repo.c/h                # 被 tag_repo_impl 包装
├── ledger_repo.c/h             # 被 ledger_repo_impl 包装
├── daily_expense_repo.c/h      # 被 daily_expense_repo_impl 包装
├── transfer_repo.c/h           # 被 transfer_repo_impl 包装
├── dca_repo.c/h                # 被 dca_repo_impl 包装
├── import_rule_repo.c/h        # 被 import_service 使用
├── asset_repo.c/h              # 被 asset_repo_impl 包装
├── auth_repo.c/h               # 被 auth_repo_impl 包装
├── transaction_repo.c/h        # 被 transaction_repo_impl 包装
├── cashflow_repo.c/h           # 被 cashflow_repo_impl 包装
├── price_history_repo.c/h      # 被 market_repo_impl 包装
├── ai_session_repo.c/h         # 被 ai_repo_impl 包装
├── ai_settings_repo.c/h        # 被 ai_repo_impl 包装
└── ai_trace_repo.c/h           # 被 ai_repo_impl 包装
```

---

## Phase 5: 清理（部分完成）

### 已删除的文件 ✅

| 文件 | 状态 |
|------|------|
| `services/daily_expense_query.c/h` | ✅ 已删除 |
| `services/daily_expense_write.c/h` | ✅ 已删除 |

### 待删除的文件（未来）

| 文件 | 依赖方 | 清理难度 |
|------|--------|----------|
| `services/report_*.c/h` | report_controller | 🟡 中（需重构 report 域） |
| `services/import_service.c/h` | import_export_controller | 🟡 中（需重构 import 域） |
| `services/category_service.c/h` | auth, admin | 🟢 低（仅 categories_seed_defaults） |
| `repositories/*_repo.c/h` | infrastructure/repo_impl | 🔴 高（impl 包装 repo） |

---

## 迁移策略

### 核心原则

1. **逐域迁移，每域独立可测**：每次只迁移一个域，完成后跑全量测试确认无回归
2. **Application Use Case 委托 Domain Rules + Infrastructure Repo**：新 usecase 不包含 SQL，只编排规则与仓储
3. **Controller 只做参数提取 + 响应封装**：Controller 不包含业务逻辑
4. **Domain 层零外部依赖**：Domain entity/rules 只引用 `core/financial/`，不引用 csilk/db/json
5. **保持 API 兼容**：前端零改动，所有 HTTP 端点路径、参数、响应格式不变

### 已完成的迁移顺序

```
Phase 1: 简单 CRUD 域（低风险）✅
  ├── tag（最简单，纯 CRUD）
  ├── category（树形结构 CRUD）
  └── ledger（RBAC + CRUD）

Phase 2: 业务编排域（中等风险）✅
  ├── daily_expense（已有 legacy service，替换调用链）
  ├── transfer（controller 含业务逻辑，需下沉）
  └── dca（controller 含业务逻辑，需下沉）

Phase 3: 报表查询域（中等风险）⚠️ 保留
  └── report（跨域依赖复杂，重构成本高）

Phase 4: 辅助域（低风险）✅
  ├── file（文件上传/解析）
  ├── import/export（保留，跨域依赖）
  ├── import_rule（保留，被 import 依赖）
  └── receipt（未开始）

Phase 5: 清理（部分完成）🔄
  ├── 删除 daily_expense_query/write.c/h ✅
  ├── 删除 report 服务文件 ⏳
  ├── 删除 import 服务文件 ⏳
  └── 更新文档 ✅
```

---

## 验证清单

每个 Task 完成后必须验证：

```bash
# 1. 编译
cd backend && cmake --build build --parallel

# 2. 单元测试
cd backend/build && ctest --output-on-failure

# 3. 集成测试
cd backend && ./tests/test_link.sh
cd backend && ./tests/test_ledgers.sh
cd backend && ./tests/test_2fa.sh
cd backend && ./tests/test_dca_cashflow.sh

# 4. 前端构建（确认 API 契约不变）
cd frontend && npm run build
```

---

## 风险缓解

| 风险 | 缓解措施 |
|------|----------|
| API 契约破坏 | Controller 保持相同路径、参数、响应格式，仅内部调用链变化 |
| 事务原子性回归 | 每个 usecase 的 BEGIN/COMMIT/ROLLBACK 模式与原 service 一致 |
| 性能回归 | 基准测试：迁移前后对比 /api/transactions 和 /api/summary 响应时间 |
| Domain 层引入外部依赖 | 严格审查 `domain/*/` 目录下的 `#include`，禁止出现 `csilk/`、`repositories/` |
| 测试覆盖缺口 | 每个迁移域至少一个 CTest 用例验证 CRUD + 业务规则 |

---

## 下一步建议

1. **重构 Report 域**：将 report 服务拆分为独立的查询服务，统一管理跨域数据访问
2. **重构 Import 域**：将 import_service 中的 CSV 解析逻辑与业务逻辑分离
3. **清理 Category Service**：将 `categories_seed_defaults` 移到 category usecase 层
4. **添加更多测试**：为新迁移的域添加单元测试覆盖
