# 登录后修改密码与系统设置页面设计规范

## 1. 概述
为了方便用户在登录 Minefolio 后自主修改账户密码，增加全新的系统设置页面 (`/settings`)，并在左侧导航栏与顶栏用户下拉菜单中提供入口。支持验证原密码并安全地更新账户哈希密码，修改成功后强制清理 Token 并引导重新登录。

## 2. 后端 API 接口设计

### 2.1 修改密码 API (`PUT /api/auth/password`)
- **权限要求**：JWT 鉴权（需在 Header 携带 `Authorization: Bearer <token>`）
- **请求体 JSON**：
  ```json
  {
    "old_password": "currentpassword",
    "new_password": "newpassword123"
  }
  ```
- **业务校验**：
  1. `old_password` 必填，`new_password` 长度需 ≥ 6 字符。
  2. 新密码不得与旧密码完全相同（若相同返回 1002 "新密码不能与原密码相同"）。
  3. 比对用户数据库中的旧密码哈希（HMAC-SHA256(pepper + old_password)）。若哈希不一致，返回 1002 ("原密码不正确")。
- **数据库更新**：
  ```sql
  UPDATE users SET password = ? WHERE id = ?
  ```
- **响应 JSON**：
  ```json
  {
    "code": 0,
    "message": "密码修改成功"
  }
  ```

## 3. 前端界面与路由设计

### 3.1 API 层与 Store (`frontend/src/api/auth.ts`)
- `authApi.changePassword({ old_password, new_password })` 挂载 `PUT /auth/password`。

### 3.2 布局与导航扩展 (`frontend/src/views/Layout.vue`)
- **侧边栏菜单**：
  - 新增 `/settings` 路由项，文本 `"系统设置"` / `t('nav.settings')`，图标 `<Setting />`。
- **顶栏用户下拉菜单**：
  - 新增 `"修改密码"` 下拉项，点击跳转至 `/settings`。

### 3.3 个人设置页面 (`frontend/src/views/Settings.vue`)
- **设计风格**：
  - 符合 Minefolio unified page layout：`.page-header`（带 `.title-accent`）、用户信息 KPI 概览卡片、表单配置卡片 (`.glass-panel`)。
- **用户信息卡片**：
  - 展示：用户名 (`username`)、账号 ID (`id`)、注册时间 (`created_at`)。
- **修改密码表单**：
  - 当前原密码 (`oldPassword`)
  - 新密码 (`newPassword`)
  - 确认新密码 (`confirmPassword`)
  - 保存按钮 (`保存新密码`)
- **提交与重新登录**：
  - 校验成功后提交至 `authApi.changePassword`。
  - 成功提示 `ElMessage.success("密码修改成功，请使用新密码重新登录")`。
  - 调用 `auth.logout()` 清除 Token 并跳转至 `/login` 页面。

## 4. 验证标准
1. `npm --prefix frontend run build` 编译 0 错误。
2. `cmake --build backend/build` 编译 0 错误。
3. `backend/tests/test_link.sh` 自动化集成测试全过，且包含旧密码校验失败与成功修改测试。
