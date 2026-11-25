import type { Category } from '@/types'

export interface CategoryCascaderNode {
  id: number
  name: string
  type: string
  parent_id: number | null
  children?: CategoryCascaderNode[]
  [key: string]: any
}

/**
 * 组装并清洗分类树（支持在线接口返回的嵌套树以及离线 SQLite 返回的扁平数组）
 * 递归剔除空 children 数组，确保 Element Plus 级联选择器在移动端正确展开且无空白子菜单
 */
export function prepareCategoryTree(
  list: Category[],
  type: 'expense' | 'income' | 'all' = 'all'
): CategoryCascaderNode[] {
  if (!list || list.length === 0) return []

  const isTree = list.some((item) => Array.isArray(item.children) && item.children.length > 0)

  let roots: any[] = []
  if (isTree) {
    roots = type === 'all' ? list : list.filter((item) => item.type === type)
  } else {
    const typedRows = type === 'all' ? list : list.filter((item) => item.type === type)
    const map = new Map<number, any>()
    for (const r of typedRows) {
      map.set(Number(r.id), { ...r, id: Number(r.id), children: [] })
    }
    for (const r of typedRows) {
      const node = map.get(Number(r.id))
      const pid = r.parent_id != null ? Number(r.parent_id) : 0
      if (pid > 0 && map.has(pid)) {
        map.get(pid).children.push(node)
      } else {
        roots.push(node)
      }
    }
  }

  const formatNodes = (nodes: any[]): CategoryCascaderNode[] => {
    return nodes.map((n) => {
      const node: CategoryCascaderNode = {
        id: Number(n.id),
        name: n.name,
        type: n.type,
        parent_id: n.parent_id != null ? Number(n.parent_id) : null,
      }
      if (Array.isArray(n.children) && n.children.length > 0) {
        const cleaned = formatNodes(n.children)
        if (cleaned.length > 0) {
          node.children = cleaned
        }
      }
      return node
    })
  }

  return formatNodes(roots)
}

/**
 * 判断指定分类 ID 是否存在于当前分类树或其任意子节点中
 */
export function isIdInTree(nodes: CategoryCascaderNode[], id: number): boolean {
  for (const n of nodes) {
    if (n.id === id) return true
    if (n.children && n.children.length > 0 && isIdInTree(n.children, id)) return true
  }
  return false
}

/**
 * 在分类树（包含父分类与各级子分类）中按名称匹配分类节点
 * 优先精确匹配，未命中时进行模糊包含匹配
 */
export function findCategoryByName(
  nodes: CategoryCascaderNode[],
  name: string
): CategoryCascaderNode | null {
  if (!name || !name.trim()) return null
  const trimmed = name.trim()

  function findExact(list: CategoryCascaderNode[]): CategoryCascaderNode | null {
    for (const n of list) {
      if (n.name === trimmed) return n
      if (n.children && n.children.length > 0) {
        const found = findExact(n.children)
        if (found) return found
      }
    }
    return null
  }

  const exact = findExact(nodes)
  if (exact) return exact

  function findFuzzy(list: CategoryCascaderNode[]): CategoryCascaderNode | null {
    for (const n of list) {
      if (n.name && (n.name.includes(trimmed) || trimmed.includes(n.name))) return n
      if (n.children && n.children.length > 0) {
        const found = findFuzzy(n.children)
        if (found) return found
      }
    }
    return null
  }

  return findFuzzy(nodes)
}

/**
 * 在分类树中查找目标分类 ID 的完整路径 [rootId, ..., targetId]
 */
export function findCategoryPath(nodes: Category[], targetId: number): number[] {
  if (!targetId || !Array.isArray(nodes) || nodes.length === 0) return []
  const numTarget = Number(targetId)

  function dfs(list: Category[], prefix: number[]): number[] | null {
    for (const node of list) {
      const next = [...prefix, Number(node.id)]
      if (Number(node.id) === numTarget) return next
      if (Array.isArray(node.children) && node.children.length > 0) {
        const hit = dfs(node.children, next)
        if (hit) return hit
      }
    }
    return null
  }

  const fromTree = dfs(nodes, [])
  if (fromTree) return fromTree

  // Flat list fallback (nodes without nested .children)
  const byId = new Map<number, Category>()
  for (const n of nodes) byId.set(Number(n.id), n)
  const path: number[] = []
  let cur: Category | undefined = byId.get(numTarget)
  const visited = new Set<number>()
  while (cur) {
    const id = Number(cur.id)
    if (visited.has(id)) break
    visited.add(id)
    path.unshift(id)
    const pid = cur.parent_id != null ? Number(cur.parent_id) : 0
    cur = pid > 0 ? byId.get(pid) : undefined
  }
  return path.length > 0 ? path : [numTarget]
}
