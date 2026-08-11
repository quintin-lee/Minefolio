# Minefolio

个人综合资产管理系统（前端 Vue 3 + 后端 C/csilk + SQLite）。

## 技术栈

- **前端**：Vue 3 + TypeScript + Vite + Element Plus + Pinia + ECharts
- **后端**：C23 + csilk + SQLite + cJSON
- **认证**：JWT (HS256)，secret 从环境变量读取
- **部署**：Docker Compose + nginx

## 快速开始

### 环境要求

- Docker 20.10+
- Docker Compose 2.0+

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
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
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
├── backend/              # C 后端（csilk 框架）
│   ├── CMakeLists.txt
│   ├── config/           # 配置文件
│   ├── sql/              # 数据库迁移脚本
│   └── src/              # 源码
│       ├── main.c        # 入口，路由注册
│       ├── auth.c        # 登录/注册/JWT
│       ├── categories.c  # 分类 CRUD + 树形查询
│       ├── assets.c      # 资产 CRUD
│       ├── transactions.c # 交易 CRUD
│       ├── daily_expenses.c # 日常收支 + 月度汇总
│       ├── tags.c        # 标签 CRUD + 自动补全
│       ├── transfers.c   # 资产间转账
│       ├── summary.c     # 汇总统计
│       ├── reports.c     # 报表（收支/资产/交易）
│       └── common/       # 公共模块
│           ├── db.h/db.c     # 数据库连接池
│           ├── jwt.h/jwt.c   # JWT 工具
│           └── response.h    # 统一响应格式
├── frontend/             # Vue 3 前端
│   ├── src/
│   │   ├── api/          # API 请求封装
│   │   ├── stores/       # Pinia 状态管理
│   │   ├── views/        # 页面组件
│   │   ├── components/   # 通用组件
│   │   └── types/        # TypeScript 类型定义
│   └── package.json
├── nginx/                # nginx 配置
│   ├── minefolio.conf           # 宿主机部署配置
│   └── minefolio.docker.conf    # Docker 部署配置
├── docs/                 # 设计文档
│   ├── superpowers/
│   │   ├── plans/2026-08-10-minefolio.md
│   │   └── specs/2026-08-10-minefolio-design.md
├── Dockerfile            # 多阶段构建（后端 + 前端 + nginx）
├── docker-compose.yml    # 容器编排
└── scripts/              # 构建/部署脚本
```

## API 端点

### 认证

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/register` | 注册 |
| POST | `/api/auth/login` | 登录 |
| GET | `/api/auth/me` | 当前用户信息 |

### 分类

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/categories` | 树形分类列表 |
| POST | `/api/categories` | 创建分类 |
| PUT | `/api/categories/:id` | 更新分类 |
| DELETE | `/api/categories/:id` | 删除分类 |

### 资产

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/assets` | 资产列表 |
| POST | `/api/assets` | 创建资产 |
| PUT | `/api/assets/:id` | 更新资产 |
| DELETE | `/api/assets/:id` | 删除资产 |
| GET | `/api/assets/:id` | 资产详情 |

### 交易

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/transactions` | 交易列表 |
| POST | `/api/transactions` | 创建交易 |
| PUT | `/api/transactions/:id` | 更新交易 |
| DELETE | `/api/transactions/:id` | 删除交易 |

### 日常收支

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/daily-expenses` | 收支列表 |
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

## 环境变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `MINEFOLIO_JWT_SECRET` | JWT 签名密钥（生产环境必须修改） | `minefolio-dev-secret-change-in-production` |
| `MINEFOLIO_DB_DSN` | SQLite 数据库路径 | `/app/data/minefolio.db` |
| `MINEFOLIO_ENABLE_CSRF` | 启用 CSRF 防护（生产推荐启用） | 未设置 |
| `HTTP_PROXY` / `HTTPS_PROXY` | 构建阶段 apt/git 代理（可选） | - |

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
# 构建
docker build --target runtime -t minefolio:latest .
docker build --target nginx -t minefolio-nginx:latest .

# 运行
docker run -d --name minefolio \
  -e MINEFOLIO_JWT_SECRET="your-secret" \
  -v minefolio-data:/app/data \
  minefolio:latest

docker run -d --name minefolio-nginx \
  --link minefolio \
  -p 80:80 \
  minefolio-nginx:latest
```

## 开发文档

详细设计文档位于 `docs/` 目录：

- `docs/superpowers/specs/2026-08-10-minefolio-design.md` — 系统设计文档
- `docs/superpowers/plans/2026-08-10-minefolio.md` — 实现计划

## License

MIT
