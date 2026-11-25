-- V034: 用户角色与账户状态管理字段 (PostgreSQL)
ALTER TABLE users ADD COLUMN role VARCHAR(32) NOT NULL DEFAULT 'user' CHECK (role IN ('admin', 'user'));
ALTER TABLE users ADD COLUMN disabled INTEGER NOT NULL DEFAULT 0;

-- 回填存量首个注册账号为管理员
UPDATE users SET role = 'admin' WHERE id = (SELECT MIN(id) FROM users);
