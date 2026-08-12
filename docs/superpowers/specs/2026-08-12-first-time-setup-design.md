# 首次部署初始化页面与注册关闭设计规范

## 1. 概述
为提升 Minefolio 的安全隐私与部署体验，在系统第一次部署启动时提供全新的**初始化设置页面 (`/setup`)**。当数据库中无用户时，自动引导管理员设置初始账号密码并写入基础数据种子；初始化完成后关闭该页面与公开注册接口，登录页面移除注册功能，确保个人财务系统的私密安全性。

## 2. 系统状态与 API 接口设计

### 2.1 系统状态查询 API (`GET /api/system/status`)
- **权限**：公开（无需 JWT）
- **实现逻辑**：
  ```sql
  SELECT COUNT(*) as user_count FROM users;
  ```
- **响应 JSON**：
  ```json
  {
    "code": 0,
    "message": "OK",
    "data": {
      "initialized": false,
      "user_count": 0
    }
  }
  ```

### 2.2 部署初始化 API (`POST /api/system/setup`)
- **权限**：仅限未初始化阶段
- **请求体 JSON**：
  ```json
  {
    "username": "admin",
    "password": "yourpassword"
  }
  ```
- **门控校验**：
  - 若 `SELECT COUNT(*) FROM users` > 0，直接返回 403 / 1004 ("系统已初始化")。
  - 用户名长度需 ≥ 2，密码长度需 ≥ 6。
- **事务执行内容**：
  1. 插入 `users` 记录 (`username`, `password_hash`)。
  2. 自动注入初始化数据种子 (`categories` 表)：
     - **income (收入)**：工资、理财收益、兼职/副业、其他收入
     - **expense (支出)**：餐饮、交通、居住、购物、娱乐、医疗、数码电子、其他支出
     - **transaction (交易)**：股票/基金、加密货币、债券/理财、定期存款
  3. 生成 JWT Token。
- **响应 JSON**：
  ```json
  {
    "code": 0,
    "message": "初始化成功",
    "data": {
      "token": "eyJhbGciOi...",
      "expires_in": 604800,
      "user": { "id": 1, "username": "admin" }
    }
  }
  ```

### 2.3 公开注册封禁 (`POST /api/auth/register`)
- 校验 `SELECT COUNT(*) FROM users`，若 `count > 0` 直接拒绝：
  ```json
  {
    "code": 1004,
    "message": "系统已完成初始化，禁止公开注册"
  }
  ```

## 3. 前端路由与页面改造

### 3.1 前端路由与导航守卫 (`frontend/src/router/index.ts`)
- **路由定义**：
  - 新增 `/setup` 路由，关联组件 `@/views/Setup.vue`。
- **全局前置守卫 (`router.beforeEach`)**：
  - 在应用初始化时先调用 `GET /api/system/status`。
  - 若 `initialized === false`：
    - 任何非 `/setup` 路由请求均自动重定向至 `/setup`。
  - 若 `initialized === true`：
    - 访问 `/setup` 自动重定向至 `/login` 或 `/dashboard`。

### 3.2 登录页面修改 (`frontend/src/views/Login.vue`)
- 移除底部 `switch-mode`（注册账号/登录系统切换按钮及 `isRegister` 状态）。
- 登录页面仅保留“用户名”、“密码”输入框与“登录系统”按钮。

### 3.3 初始化设置页面 (`frontend/src/views/Setup.vue`)
- **视觉风格**：深色玻璃拟态容器（Glassmorphic Dark Theme），保持 Minefolio 统一设计美学。
- **表单内容**：
  - 标题：“Minefolio 部署初始化”
  - 提示：“欢迎使用 Minefolio 资产管理系统！请设置管理员账号与初始配置。”
  - 表单项：
    1. 管理员用户名（`username`）
    2. 管理员密码（`password`）
    3. 确认密码（`confirmPassword`）
- **交互逻辑**：
  - 表单校验通过后调用 `POST /api/system/setup`。
  - 成功后自动将返回的 Token 存入 Auth Store，提示“初始化成功”，并自动跳转至主页路由 `/dashboard`。

## 4. 验证标准
1. `npm --prefix frontend run build` 无 TypeScript/Vite 编译错误。
2. `cmake --build backend/build` 无 C 语言编译错误。
3. `backend/tests/test_link.sh` 自动化测试集 100% 通过。
