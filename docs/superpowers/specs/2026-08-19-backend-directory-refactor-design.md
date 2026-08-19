# Backend 目录结构重构设计

## 目标

将 `backend/src/` 从当前的扁平结构（24 个文件平铺）重构为控制层 / 服务层 / 仓库层分明的分层架构，同时拆分过大的文件。

## 现状分析

| 问题 | 证据 |
|---|---|
| 扁平结构，无领域隔离 | 24 个文件全部在 `src/` 根目录 |
| main.c 臃肿 | 49 条 forward declaration，395 行 |
| reports.c 过大 | 796 行，混合 expense/asset/summary 三类报告 |
| import_export.c 过大 | 629 行，混合交易和日支出 CSV 逻辑 |
| SQL 散落在 handler 中 | 无 DAO 层，业务逻辑与 DB 查询未分离 |
| 中间件散落在 main.c | cors/ csrf / jwt 中间件内联在路由注册处 |

## 目标结构

```
backend/src/
│
├── main.c                          精简到 ~60 行，只注册路由
├── swagger_types.h                 OpenAPI 反射类型（保持不变）
│
├── models/                         数据库表映射结构体
│   ├── user.h
│   ├── category.h
│   ├── asset.h
│   ├── transaction.h
│   ├── daily_expense.h
│   ├── tag.h
│   └── transfer.h
│
├── dtos/                           请求/响应 DTO
│   ├── request.h                   所有 POST/PUT 请求体 struct
│   └── response.h                  所有响应体 struct
│
├── repositories/                   纯 DB 查询（static 函数）
│   ├── user_repo.c
│   ├── category_repo.c
│   ├── asset_repo.c
│   ├── transaction_repo.c
│   ├── daily_expense_repo.c
│   ├── tag_repo.c
│   └── transfer_repo.c
│
├── services/                       业务逻辑（调用 repositories）
│   ├── auth_service.c
│   ├── asset_service.c
│   ├── category_service.c
│   ├── daily_expense_service.c
│   ├── tag_service.c
│   ├── transfer_service.c
│   ├── transaction_service.c
│   └── report_service.c
│
├── controllers/                    HTTP handler（薄层）
│   ├── auth_controller.c
│   ├── asset_controller.c
│   ├── category_controller.c
│   ├── daily_expense_controller.c
│   ├── tag_controller.c
│   ├── transfer_controller.c
│   ├── transaction_controller.c
│   ├── report_controller.c
│   └── import_export_controller.c
│
├── middlewares/                    中间件
│   ├── jwt_middleware.c
│   ├── cors_middleware.c
│   └── csrf_middleware.c
│
├── config/                         服务配置与初始化
│   ├── db_config.c                 ← 从 db.c 拆分：pool 初始化 + migration
│   ├── app_config.c                ← 从 config.c 保留
│   └── key_manager.c               ← 从 auth_key.c 移入
│
└── common/                         共享基础设施（不变）
    ├── balance.c/h
    ├── config.c/h
    ├── db.c/h
    ├── jwt.c/h
    ├── response.h
    └── tx_types.c/h
```

## 数据流向

```
HTTP Request
    │
    ▼
controllers/   ← 解析 query/param/body → 调用 service → respond_ok/respond_error
    │
    ▼
services/      ← 业务规则、事务编排、状态变更；不含 HTTP 代码
    │
    ▼
repositories/  ← 只负责 SQL，接收/返回 models/* 结构体；不含业务规则
    │
    ▼
db.c           ← csilk_db_query_param_json / csilk_db_exec
```

## 各层职责说明

### controllers/（控制层）
- 接收 `csilk_ctx_t*`，解析路径参数、查询参数、请求体
- 调用对应的 service 函数
- 用 `respond_ok / respond_error` 返回结果
- **不含**：SQL、业务计算、状态变更逻辑

### services/（服务层）
- 实现业务规则：余额联动、持仓追踪、分类树构建、转账事务
- 调用 repositories 获取/写入数据
- **不含**：HTTP 代码、JSON 序列化

### repositories/（数据访问层）
- 每个实体一个 `.c` 文件，含该表所有 SQL 查询
- 函数签名：`static` 内部函数 + 对外前向声明
- 输入输出使用 `models/*` 结构体
- **不含**：业务规则、HTTP 代码

### models/（实体层）
- C struct 对应数据库表
- 字段名与 JSON key 一致（便于反射序列化）
- 仅 `.h` 文件，无 `.c` 实现

### dtos/（数据传输层）
- 从 `swagger_types.h` 拆分出的请求体（req）和响应体（resp）struct
- 用于 OpenAPI 反射注册和类型化接口

### middlewares/（中间件层）
- JWT 鉴权、CORS、CSRF 各自独立文件
- 从 `main.c` 的匿名 static 函数提取

### config/（配置层）
- `db_config.c`：DB pool 初始化、migration 执行
- `key_manager.c`：RSA 密钥管理（从 `auth_key.c` 移入）

## 大文件拆分映射

| 原文件 | 拆分去向 |
|---|---|
| `auth.c` (403 行) | `controllers/auth_controller.c` + `services/auth_service.c` + `middlewares/jwt_middleware.c` |
| `auth_key.c/h` (89 行) | `config/key_manager.c` |
| `assets.c` (366 行) | `controllers/asset_controller.c` + `services/asset_service.c` |
| `asset_logs.c` (72 行) | `repositories/asset_repo.c`（查询部分）+ `controllers/` |
| `categories.c` (501 行) | `controllers/category_controller.c` + `services/category_service.c` + `repositories/category_repo.c` |
| `daily_expenses.c` (544 行) | `controllers/daily_expense_controller.c` + `services/daily_expense_service.c` |
| `tags.c` (126 行) | `controllers/tag_controller.c` + `services/tag_service.c` |
| `transactions.c` (611 行) | `controllers/transaction_controller.c` + `services/transaction_service.c` + `repositories/transaction_repo.c` |
| `transfers.c` (122 行) | `controllers/transfer_controller.c` + `services/transfer_service.c` |
| `reports.c` (796 行) | `controllers/report_controller.c` + `services/report_service.c` + `repositories/transaction_repo.c`（报告查询） |
| `import_export.c` (629 行) | `controllers/import_export_controller.c` + `services/` + `repositories/` |
| `main.c` (395 行) | 精简为 ~60 行，仅路由注册 |

## 构建系统

`CMakeLists.txt` 无需修改。现有 glob 模式：
```cmake
file(GLOB SOURCES "src/*.c" "src/common/*.c")
target_include_directories(minefolio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
```
递归 glob 会自动发现所有子目录下的 `.c` 文件，`include` 路径只需指向 `src/` 根即可访问所有子目录。

## 旧文件处理

直接迁移：移动/创建新文件后立即删除旧文件。不保留并行结构。

## 验证标准

1. `cmake --build backend/build --parallel` 编译通过
2. `bash backend/tests/test_link.sh` 全部 103 项 PASS
3. `http://localhost:8080/openapi.json` 仍包含所有 schema
4. `http://localhost:8080/docs` Swagger UI 正常渲染
