import type { PageResult } from '@/types'

/**
 * 循环拉取分页接口直到获取全量数据（供下拉选择器、树构建、实体索引等全集消费方使用）。
 * 兼容直接返回数组的测试 mock。
 */
export async function fetchAllPages<T>(
  fetchPage: (page: number, pageSize: number) => Promise<PageResult<T> | T[]>,
  pageSize = 500,
  maxPages = 20,
): Promise<T[]> {
  const collected: T[] = []
  for (let page = 1; page <= maxPages; page++) {
    const res = await fetchPage(page, pageSize)
    if (Array.isArray(res)) {
      collected.push(...res)
      break
    }
    const list = Array.isArray(res?.list) ? res.list : []
    collected.push(...list)
    const total = typeof res?.total === 'number' ? res.total : collected.length
    if (list.length === 0 || list.length < pageSize || collected.length >= total) {
      break
    }
  }
  return collected
}
