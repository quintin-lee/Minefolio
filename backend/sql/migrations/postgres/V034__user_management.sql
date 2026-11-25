-- V034: 用户角色与账户状态管理字段 (PostgreSQL)
-- 幂等化: ADD COLUMN IF NOT EXISTS, 防止部分执行残留导致重启卡死
ALTER TABLE users ADD COLUMN IF NOT EXISTS role VARCHAR(32) NOT NULL DEFAULT 'user';
ALTER TABLE users ADD COLUMN IF NOT EXISTS disabled INTEGER NOT NULL DEFAULT 0;

-- CHECK 约束: 若尚不存在则补齐 (PG13+ 无原生 ADD CONSTRAINT IF NOT EXISTS, 用 DO 块检查)
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'public.users'::regclass
          AND (
              conname = 'users_role_check'
              OR (contype = 'c' AND pg_get_constraintdef(oid) ILIKE '%role%')
          )
    ) THEN
        ALTER TABLE users ADD CONSTRAINT users_role_check CHECK (role IN ('admin', 'user'));
    END IF;
END
$$;

-- 回填存量首个注册账号为管理员
UPDATE users SET role = 'admin' WHERE id = (SELECT MIN(id) FROM users);
