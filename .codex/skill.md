# Minefolio 项目说明

## 技术栈
- 后端：C23 + csilk 框架 + SQLite
- 前端：Vue 3 + TypeScript + Vite + Element Plus + ECharts
- 认证：JWT (HS256)

## 目录结构

```
backend/           # C 后端
  CMakeLists.txt
  config/minefolio.yaml
  sql/migration.sql
  src/
    main.c         # 入口
    common/        # 公共模块 (response, db, jwt)
    auth.c         # 登录/注册
    categories.c   # 分类 CRUD
    assets.c       # 资产 CRUD
    transactions.c # 交易记录
    daily_expenses.c # 日常收支
    tags.c         # 标签管理
    transfers.c    # 资产转账
    reports.c      # 报表
frontend/          # Vue 3 前端
  package.json
  vite.config.ts
  src/
    main.ts        # 入口
    views/         # 页面
    components/    # 组件
    api/           # API 调用
    stores/        # Pinia store
    types/         # TypeScript 类型
    locales/       # 中文 i18n
```

## 后端构建与运行
```bash
cd backend/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
export MINEFOLIO_JWT_SECRET="your-secret"
export MINEFOLIO_DB_DSN="./data/minefolio.db"
mkdir -p data
./minefolio
```

## 前端开发
```bash
cd frontend
npm install
npm run dev
# 访问 http://localhost:5173
```

## 关键 API
- POST /api/auth/login, /api/auth/register
- GET /api/categories — 树形分类
- GET/POST /api/assets, PUT/DELETE /api/assets/:id
- GET/POST /api/transactions, PUT/DELETE /api/transactions/:id
- GET/POST /api/daily-expenses, PUT/DELETE /api/daily-expenses/:id
- GET /api/daily-expenses/monthly?year=&month=
- GET/POST /api/tags, PUT/DELETE /api/tags/:id
- POST /api/transfers
- GET /api/reports/expense/monthly, /expense/trend, /asset/trend 等
- GET /api/summary
