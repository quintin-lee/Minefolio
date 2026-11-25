-- V036: 清理重复分类数据并补齐部分唯一索引，彻底消除顶级分类 (parent_id IS NULL) 及并发播种重复 (SQLite)

-- 1. 建立根分类映射表 (parent_id IS NULL): 同一账本/用户、同类型、同名称保留最小 ID
CREATE TEMP TABLE _mf_v036_root_map AS
SELECT
    c.id AS id,
    (
        SELECT MIN(c2.id)
        FROM categories c2
        WHERE c2.parent_id IS NULL
          AND COALESCE(c2.ledger_id, 0) = COALESCE(c.ledger_id, 0)
          AND (CASE WHEN COALESCE(c2.ledger_id, 0) > 0 THEN 0 ELSE c2.user_id END) =
              (CASE WHEN COALESCE(c.ledger_id, 0) > 0 THEN 0 ELSE c.user_id END)
          AND c2.type = c.type
          AND c2.name = c.name
    ) AS canon_id
FROM categories c
WHERE c.parent_id IS NULL;

-- 2. 建立子分类映射表 (parent_id IS NOT NULL): 按规范化后的父分类 ID 分组保留最小 ID
CREATE TEMP TABLE _mf_v036_child_map AS
SELECT
    c.id AS id,
    COALESCE((SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = c.parent_id), c.parent_id) AS eff_pid,
    (
        SELECT MIN(c2.id)
        FROM categories c2
        WHERE c2.parent_id IS NOT NULL
          AND COALESCE(c2.ledger_id, 0) = COALESCE(c.ledger_id, 0)
          AND (CASE WHEN COALESCE(c2.ledger_id, 0) > 0 THEN 0 ELSE c2.user_id END) =
              (CASE WHEN COALESCE(c.ledger_id, 0) > 0 THEN 0 ELSE c.user_id END)
          AND COALESCE((SELECT rm2.canon_id FROM _mf_v036_root_map rm2 WHERE rm2.id = c2.parent_id), c2.parent_id) =
              COALESCE((SELECT rm1.canon_id FROM _mf_v036_root_map rm1 WHERE rm1.id = c.parent_id), c.parent_id)
          AND c2.type = c.type
          AND c2.name = c.name
    ) AS canon_id
FROM categories c
WHERE c.parent_id IS NOT NULL;

-- 3. 迁移外键并删除重复子分类（先删冲突子分类再重挂非冲突子分类，避免触发 V002 UNIQUE(user_id, name, parent_id)）
UPDATE assets
SET category_id = (SELECT cm.canon_id FROM _mf_v036_child_map cm WHERE cm.id = assets.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id);

UPDATE transactions
SET category_id = (SELECT cm.canon_id FROM _mf_v036_child_map cm WHERE cm.id = transactions.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id);

UPDATE daily_expenses
SET category_id = (SELECT cm.canon_id FROM _mf_v036_child_map cm WHERE cm.id = daily_expenses.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id);

UPDATE import_rules
SET category_id = (SELECT cm.canon_id FROM _mf_v036_child_map cm WHERE cm.id = import_rules.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id);

UPDATE categories
SET parent_id = (SELECT cm.canon_id FROM _mf_v036_child_map cm WHERE cm.id = categories.parent_id)
WHERE parent_id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id)
  AND NOT EXISTS (
      SELECT 1 FROM categories c2
      WHERE c2.user_id = categories.user_id
        AND c2.name = categories.name
        AND c2.parent_id = (SELECT cm2.canon_id FROM _mf_v036_child_map cm2 WHERE cm2.id = categories.parent_id)
  );

DELETE FROM categories
WHERE id IN (SELECT id FROM _mf_v036_child_map WHERE id != canon_id);

-- 4. 将重复根分类下未冲突的子分类重挂到规范根分类
UPDATE categories
SET parent_id = (SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = categories.parent_id)
WHERE parent_id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

-- 5. 迁移根分类外键并删除重复根分类
UPDATE assets
SET category_id = (SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = assets.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

UPDATE transactions
SET category_id = (SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = transactions.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

UPDATE daily_expenses
SET category_id = (SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = daily_expenses.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

UPDATE import_rules
SET category_id = (SELECT rm.canon_id FROM _mf_v036_root_map rm WHERE rm.id = import_rules.category_id)
WHERE category_id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

DELETE FROM categories
WHERE id IN (SELECT id FROM _mf_v036_root_map WHERE id != canon_id);

-- 6. 清理临时表
DROP TABLE IF EXISTS _mf_v036_child_map;
DROP TABLE IF EXISTS _mf_v036_root_map;

-- 7. 建立部分唯一索引，覆盖 parent_id IS NULL 场景及账本/历史用户两类范围
CREATE UNIQUE INDEX IF NOT EXISTS uq_categories_ledger_parent_type_name
    ON categories (ledger_id, type, COALESCE(parent_id, 0), name)
    WHERE ledger_id IS NOT NULL AND ledger_id > 0;

CREATE UNIQUE INDEX IF NOT EXISTS uq_categories_user_legacy_parent_type_name
    ON categories (user_id, type, COALESCE(parent_id, 0), name)
    WHERE ledger_id IS NULL OR ledger_id <= 0;
