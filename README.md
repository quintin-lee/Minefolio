# Minefolio

个人综合资产管理系统（前端 Vue 3 + 后端 C/csilk v0.5.0 + SQLite）。

## 技术栈

- **前端**：Vue 3 + TypeScript + Vite + Element Plus + Pinia + ECharts
- **后端**：C23 + csilk v0.5.0 + SQLite + yyjson
- **认证**：JWT (HS256)，secret 从环境变量读取
- **架构**：三层架构 — Controllers → Services → Repositories
- **部署**：Docker Compose + nginx

## 快速开始

### 环境要求

- Docker 20.10+
- Docker Compose 2.0+
- Node.js 18+（本地开发）

### 启动

```bash
cp .env.example .env
# 编辑 .env 设置 MINEFOLIO_JWT_SECRET
docker compose up -d --build
```

访问 http://localhost

### 本地开发

**后端：**
```bash
cd backend
cmake -B build -G "Unix Makefiles"
cmake --build build --parallel
./build/minefolio
```

**前端：**
```bash
cd frontend
npm install
npm run dev
```

## 项目结构

```
├── backend/                      # C 后端（csilk v0.5.0 框架）
│   ├── CMakeLists.txt
│   ├── config/                   # 配置文件 (db.json 自动生成)
│   ├── sql/                      # 数据库迁移脚本
│   └── src/
│       ├── main.c                # 入口，中间件 + 路由注册
│       ├── controllers/          # HTTP 层：解析请求，调用 service
│       │   ├── auth_controller.c/h
│       │   ├── asset_controller.c/h
│       │   ├── category_controller.c/h
│       │   ├── transaction_controller.c/h
│       │   ├── daily_expense_controller.c/h
│       │   ├── tag_controller.c/h
│       │   ├── transfer_controller.c/h
│       │   ├── report_controller.c/h
│       │   ├── import_export_controller.c/h
│       │   └── admin_controller.c/h
│       ├── services/             # 业务层：编排 repo + balance
│       │   ├── auth_service.c/h
│       │   ├── admin_service.c/h
│       │   ├── asset_service.c/h
│       │   ├── category_service.c/h
│       │   ├── transaction_query.c/h
│       │   ├── transaction_write.c/h
│       │   ├── daily_expense_query.c/h
│       │   ├── daily_expense_write.c/h
│       │   ├── tag_service.c/h
│       │   ├── transfer_service.c/h
│       │   ├── report_*.c/h      # 报表服务（未拆分）
│       │   ├── export_service.c/h
│       │   └── import_service.c/h
│       ├── repositories/         # 数据层：所有 SQL 查询
│       │   ├── auth_repo.c/h
│       │   ├── asset_repo.c/h
│       │   ├── category_repo.c/h
│       │   ├── transaction_repo.c/h
│       │   ├── daily_expense_repo.c/h
│       │   ├── tag_repo.c/h
│       │   └── transfer_repo.c/h
│       ├── common/               # 公共模块
│       │   ├── db.h/.c           # 数据库连接池
│       │   ├── jwt.h/.c          # JWT 工具
│       │   ├── balance.h/.c      # 余额计算
│       │   ├── tx_types.h/.c     # 交易类型注册表
│       │   ├── response.h        # 统一响应格式
│       │   ├── ctx.h             # 上下文助手 (ctx_user_id)
│       │   ├── config.h/.c       # 配置持久化
│       │   └── csv_utils.h/.c    # CSV 解析工具
│       ├── config/               # 密钥管理
│       │   ├── key_manager.h/.c  # RSA 密钥对
│       │   └── db_config.h/.c    # 数据库配置
│       ├── middlewares/          # 中间件
│       │   ├── jwt_middleware.c/h
│       │   ├── csrf_middleware.c/h
│       │   ├── cors_middleware.c/h
│       │   ├── rate_limit.c/h
│       │   └── security_headers.c/h
│       ├── dtos/                 # 请求/响应类型定义
│       │   ├── request.h
│       │   └── response.h
│       └── models/               # 领域模型（未广泛使用）
│           ├── asset.h
│           ├── category.h
│           └── ...
├── frontend/                     # Vue 3 前端
│   ├── src/
│   │   ├── api/                  # API 请求封装
│   │   ├── stores/               # Pinia 状态管理
│   │   ├── views/                # 页面组件
│   │   ├── components/           # 通用组件
│   │   └── types/                # TypeScript 类型定义
│   └── package.json
├── nginx/                        # nginx 配置
├── docs/                         # 设计文档
│   ├── superpowers/
│   │   ├── specs/                # 设计规格
│   │   └── plans/                # 实现计划
│   └── security-audit.md         # 安全审计报告
├── Dockerfile                    # 多阶段构建
├── docker-compose.yml            # 容器编排
└── AGENTS.md                     # Agent 开发指南
```

## API 端点

### 认证

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/register` | 注册 |
| POST | `/api/auth/login` | 登录 |
| GET | `/api/auth/public-key` | 获取 RSA 公钥（无认证） |
| GET | `/api/auth/me` | 当前用户信息 |
| PUT | `/api/auth/password` | 修改密码 |

### 系统

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/system/status` | 系统状态（无认证） |
| POST | `/api/system/setup` | 初始化系统（无认证） |

### 分类

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/categories` | 树形分类列表 |
| POST | `/api/categories` | 创建分类 |
| PUT | `/api/categories/:id` | 更新分类 |
| DELETE | `/api/categories/:id` | 删除分类 |
| GET | `/api/categories/:id/children` | 子分类计数 |

### 资产

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/assets` | 资产列表（分页） |
| POST | `/api/assets` | 创建资产 |
| PUT | `/api/assets/:id` | 更新资产（投资类自动重算） |
| DELETE | `/api/assets/:id` | 删除资产 |
| GET | `/api/assets/:id` | 资产详情（含历史交易） |

### 交易

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/transactions` | 交易列表（分页） |
| POST | `/api/transactions` | 创建交易 |
| PUT | `/api/transactions/:id` | 更新交易 |
| DELETE | `/api/transactions/:id` | 删除交易 |
| GET | `/api/transactions/monthly` | 月度汇总 |

### 日常收支

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/daily-expenses` | 收支列表（分页） |
| POST | `/api/daily-expenses` | 创建收支 |
| PUT | `/api/daily-expenses/:id` | 更新收支 |
| DELETE | `/api/daily-expenses/:id` | 删除收支 |
| GET | `/api/daily-expenses/monthly` | 月度汇总 |

### 标签

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/tags` | 标签列表 |
| POST | `/api/tags` | 创建标签 |
| PUT | `/api/tags/:id` | 更新标签 |
| DELETE | `/api/tags/:id` | 删除标签 |
| GET | `/api/tags/suggestions` | 标签自动补全 |

### 转账

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/transfers` | 资产间转账 |

### 汇总

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/summary` | 资产净值汇总 |

### 报表

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/reports/expense/monthly` | 月度收支报表 |
| GET | `/api/reports/expense/trend` | 收支趋势 |
| GET | `/api/reports/expense/category` | 支出分类占比 |
| GET | `/api/reports/expense/tag` | 标签支出分析 |
| GET | `/api/reports/asset/trend` | 净资产趋势 |
| GET | `/api/reports/asset/breakdown` | 资产分布 |
| GET | `/api/reports/transaction/performance` | 交易表现 |
| GET | `/api/reports/asset/summary` | 资产总览 |
| GET | `/api/reports/holdings` | 持仓明细 |

### 导入导出

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/export/transactions` | 导出交易 CSV |
| POST | `/api/import/transactions` | 导入交易 CSV |
| GET | `/api/export/daily-expenses` | 导出收支 CSV |
| POST | `/api/import/daily-expenses` | 导入收支 CSV |

## 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `MINEFOLIO_JWT_SECRET` | JWT 签名密钥（生产环境必须设置） | 未设置（开发模式） |
| `MINEFOLIO_DB_DRIVER` | 数据库驱动：`sqlite` 或 `postgres` | `sqlite` |
| `MINEFOLIO_DB_DSN` | 数据库连接字符串 | `./data/minefolio.db` |
| `MINEFOLIO_ENABLE_CSRF` | 启用 CSRF 防护 | 未设置 |

## 部署

### Docker Compose（推荐）

```bash
docker compose up -d --build
```

服务说明：
- `minefolio`：后端 C 服务，监听 8080（内部）
- `minefolio-nginx`：前端静态文件 + API 反向代理，监听 80

### 手动部署

```bash
# 构建后端运行时镜像
docker build --target runtime -t minefolio:latest .

# 运行后端
docker run -d --name minefolio \
  -e MINEFOLIO_JWT_SECRET="your-secret" \
  -v minefolio-data:/app/data \
  minefolio:latest
```

## 开发文档

详细设计文档位于 `docs/` 目录：

- `docs/security-audit.md` — 安全审计报告（全部修复）
- `docs/superpowers/specs/` — 设计规格
- `docs/superpowers/plans/` — 实现计划
- `AGENTS.md` — Agent 开发指南

## License

MIT
