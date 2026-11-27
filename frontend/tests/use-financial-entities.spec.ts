import { describe, it, expect, vi, beforeEach } from 'vitest'
import {
  buildEntityRegex,
  replaceTextNodesWithEntities,
  isStopword,
  useFinancialEntities,
  resetFinancialEntities,
} from '../src/composables/useFinancialEntities'

vi.mock('@/api/accounts', () => ({ accountsApi: { list: vi.fn() } }))
vi.mock('@/api/assets', () => ({ assetsApi: { list: vi.fn() } }))
vi.mock('@/api/reports', () => ({ reportsApi: { holdings: vi.fn() } }))

import { accountsApi } from '@/api/accounts'
import { assetsApi } from '@/api/assets'
import { reportsApi } from '@/api/reports'

describe('useFinancialEntities', () => {
  it('filters out system stopwords and single characters', () => {
    expect(isStopword('默认账户')).toBe(true)
    expect(isStopword('现金账户')).toBe(true)
    expect(isStopword('卡')).toBe(true)
    expect(isStopword('腾讯控股')).toBe(false)
    expect(isStopword('招商银行')).toBe(false)
  })

  it('sorts patterns by length descending for longest-first matching', () => {
    const names = ['招商银行', '招商银行储蓄卡', '银行']
    const regex = buildEntityRegex(names)
    const match = '我的招商银行储蓄卡已到期'.match(regex!)
    expect(match?.[0]).toBe('招商银行储蓄卡')
  })

  it('replaces only plain text and ignores tags/code blocks', () => {
    const html = '<p>请查看腾讯控股行情，参考 <code>腾讯控股代码</code> 和 <a href="#">腾讯控股官网</a></p>'
    const lookup = new Map([['腾讯控股', { type: 'asset' as const, id: 101, name: '腾讯控股' }]])
    const result = replaceTextNodesWithEntities(html, lookup)
    expect(result).toContain('<span class="mf-entity-badge" data-entity-type="asset" data-entity-id="101">腾讯控股</span>')
    expect(result).toContain('<code>腾讯控股代码</code>')
    expect(result).toContain('<a href="#">腾讯控股官网</a>')
  })

  it('prevents DOM XSS injection when text node contains decoded HTML characters', () => {
    const html = '<p>关注腾讯控股 <img src=x onerror=alert(1)> 测试</p>'
    const lookup = new Map([['腾讯控股', { type: 'asset' as const, id: 101, name: '腾讯控股' }]])
    const result = replaceTextNodesWithEntities(html, lookup)
    expect(result).toContain('<span class="mf-entity-badge" data-entity-type="asset" data-entity-id="101">腾讯控股</span>')
    // Must NOT have unescaped active onerror handlers instantiated as live elements
    expect(result).not.toContain('onerror=alert(1)')
  })

  it('wraps currency amounts in .mf-amount-pill', () => {
    const html = '<p>招商银行余额为 ¥12,850.50，美元资产 $3,200</p>'
    const lookup = new Map([['招商银行', { type: 'account' as const, id: 1, name: '招商银行' }]])
    const result = replaceTextNodesWithEntities(html, lookup)
    expect(result).toContain('<span class="mf-amount-pill">¥12,850.50</span>')
    expect(result).toContain('<span class="mf-amount-pill">$3,200</span>')
  })

  it('clears cached entities on resetFinancialEntities so next load refetches', async () => {
    resetFinancialEntities()
    vi.clearAllMocks()
    ;(accountsApi.list as any).mockResolvedValueOnce({ list: [{ id: 1, name: '招商银行', institution: 'bank_card' }], total: 1, page: 1, page_size: 500 })
    ;(assetsApi.list as any).mockResolvedValueOnce({ list: [{ id: 10, name: '茅台', type: 'stock', currency: 'CNY', current_value: 1000 }] })
    ;(reportsApi.holdings as any).mockResolvedValueOnce({ list: [] })

    const { loadEntities, getEntityDetail, linkFinancialEntities } = useFinancialEntities()
    await loadEntities()
    expect(getEntityDetail('account', 1)?.name).toBe('招商银行')
    expect(linkFinancialEntities('<p>茅台</p>')).toContain('mf-entity-badge')

    resetFinancialEntities()
    expect(getEntityDetail('account', 1)).toBeNull()
    expect(linkFinancialEntities('<p>茅台</p>')).not.toContain('mf-entity-badge')

    ;(accountsApi.list as any).mockResolvedValueOnce({ list: [{ id: 2, name: '工商银行', institution: 'bank_card' }], total: 1, page: 1, page_size: 500 })
    ;(assetsApi.list as any).mockResolvedValueOnce({ list: [] })
    ;(reportsApi.holdings as any).mockResolvedValueOnce({ list: [] })
    await loadEntities()
    expect(accountsApi.list).toHaveBeenCalledTimes(2)
    expect(getEntityDetail('account', 2)?.name).toBe('工商银行')
  })

  it('paginates assets beyond 100 items and increments entitiesVersion on load/reset', async () => {
    resetFinancialEntities()
    vi.clearAllMocks()

    const { loadEntities, getEntityDetail, entitiesVersion } = useFinancialEntities()
    const v0 = entitiesVersion.value

    const page1 = Array.from({ length: 100 }, (_, i) => ({
      id: i + 1,
      name: `AssetPageOne_${i + 1}`,
      type: 'stock',
      currency: 'CNY',
      current_value: 100,
    }))
    const page2 = [
      { id: 101, name: '茅台定投基金', type: 'fund', currency: 'CNY', current_value: 2000 },
    ]
    ;(accountsApi.list as any).mockResolvedValueOnce({ list: [], total: 0, page: 1, page_size: 500 })
    ;(assetsApi.list as any)
      .mockResolvedValueOnce({ list: page1, total: 101 })
      .mockResolvedValueOnce({ list: page2, total: 101 })
    ;(reportsApi.holdings as any).mockResolvedValueOnce({ holdings: [] })

    await loadEntities()

    expect(assetsApi.list).toHaveBeenCalledTimes(2)
    expect(assetsApi.list).toHaveBeenNthCalledWith(2, { page: 2, page_size: 100 })
    expect(getEntityDetail('asset', 101)?.name).toBe('茅台定投基金')
    expect(entitiesVersion.value).toBe(v0 + 1)

    resetFinancialEntities()
    expect(entitiesVersion.value).toBe(v0 + 2)
  })
})
