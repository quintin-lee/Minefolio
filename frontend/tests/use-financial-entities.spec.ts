import { describe, it, expect } from 'vitest'
import { buildEntityRegex, replaceTextNodesWithEntities, isStopword } from '../src/composables/useFinancialEntities'

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
})
