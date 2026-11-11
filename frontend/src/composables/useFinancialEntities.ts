/**
 * @file useFinancialEntities.ts
 * @description Composable for extracting financial entity dictionaries (accounts & assets),
 * matching entity mentions in AI chat messages, and providing real-time holding & valuation metrics.
 */

import { ref } from 'vue'
import { accountsApi } from '@/api/accounts'
import { assetsApi } from '@/api/assets'
import { reportsApi, type HoldingsItem } from '@/api/reports'

export interface EntityDetail {
  id: string | number
  name: string
  type: 'asset' | 'account'
  icon?: string
  institution?: string
  accountName?: string
  assetType?: string
  currentValue?: number
  costBasis?: number
  quantity?: number
  floatingPnl?: number
  floatingPct?: number
  currency?: string
  assetCount?: number
}

export interface EntityMatch {
  id: string | number
  name: string
  type: 'asset' | 'account'
}

const STOPWORDS = new Set([
  '默认账户',
  '现金账户',
  '总资产',
  '总负债',
  '净资产',
  '资产',
  '账户',
  '资金',
  '收益',
  '支出',
  '收入',
  '账单',
  '金额',
  '余额',
])

export function isStopword(word: string): boolean {
  if (!word || word.trim().length < 2) return true
  return STOPWORDS.has(word.trim())
}

function escapeRegExp(str: string): string {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

export function buildEntityRegex(names: string[]): RegExp | null {
  const valid = names
    .filter(n => !isStopword(n))
    .sort((a, b) => b.length - a.length)
  if (valid.length === 0) return null
  const pattern = valid.map(escapeRegExp).join('|')
  return new RegExp(pattern, 'g')
}

const AMOUNT_REGEX = /(?:¥|\$|€|£)\s*\d+(?:,\d{3})*(?:\.\d+)?/g

export function replaceTextNodesWithEntities(
  html: string,
  lookup: Map<string, EntityMatch>
): string {
  if (!html) return html
  const names = Array.from(lookup.keys())
  const entityRegex = buildEntityRegex(names)

  if (typeof DOMParser !== 'undefined') {
    try {
      const parser = new DOMParser()
      const doc = parser.parseFromString(`<body>${html}</body>`, 'text/html')
      const ignoreTags = new Set(['A', 'CODE', 'PRE', 'SCRIPT', 'STYLE', 'BUTTON'])

      function walk(node: Node) {
        if (node.nodeType === 3) { // Text node
          const parent = node.parentElement
          if (parent && ignoreTags.has(parent.tagName.toUpperCase())) {
            return
          }
          const text = node.nodeValue || ''
          if (!text.trim()) return

          const hasEntity = entityRegex ? entityRegex.test(text) : false
          if (entityRegex) entityRegex.lastIndex = 0
          const hasAmount = AMOUNT_REGEX.test(text)
          AMOUNT_REGEX.lastIndex = 0

          if (!hasEntity && !hasAmount) return

          const entityPattern = entityRegex ? entityRegex.source : ''
          const amountPattern = '(?:(?:¥|\\$|€|£)\\s*\\d+(?:,\\d{3})*(?:\\.\\d+)?)'
          const combinedPattern = entityPattern
            ? `(${entityPattern})|(${amountPattern})`
            : `(${amountPattern})`
          const combinedRegex = new RegExp(combinedPattern, 'g')

          const fragment = doc.createDocumentFragment()
          let lastIndex = 0
          let m: RegExpExecArray | null

          while ((m = combinedRegex.exec(text)) !== null) {
            if (m.index > lastIndex) {
              fragment.appendChild(doc.createTextNode(text.slice(lastIndex, m.index)))
            }
            const matched = m[0]
            const item = lookup.get(matched)
            if (item) {
              const span = doc.createElement('span')
              span.className = 'mf-entity-badge'
              span.setAttribute('data-entity-type', item.type)
              span.setAttribute('data-entity-id', String(item.id))
              span.textContent = matched
              fragment.appendChild(span)
            } else if (/(?:¥|\$|€|£)/.test(matched)) {
              const span = doc.createElement('span')
              span.className = 'mf-amount-pill'
              span.textContent = matched
              fragment.appendChild(span)
            } else {
              fragment.appendChild(doc.createTextNode(matched))
            }
            lastIndex = m.index + matched.length
          }

          if (lastIndex < text.length) {
            fragment.appendChild(doc.createTextNode(text.slice(lastIndex)))
          }

          node.parentNode?.replaceChild(fragment, node)
        } else if (node.nodeType === 1) { // Element node
          const el = node as HTMLElement
          if (!ignoreTags.has(el.tagName.toUpperCase())) {
            Array.from(node.childNodes).forEach(walk)
          }
        }
      }

      Array.from(doc.body.childNodes).forEach(walk)
      return doc.body.innerHTML
    } catch {
      // fallback
    }
  }

  return html
}

function getAssetIcon(type?: string): string {
  if (type === 'crypto') return 'ph:currency-btc-bold'
  if (type === 'stock') return 'ph:chart-line-up-bold'
  if (type === 'fund') return 'ph:chart-pie-slice-bold'
  if (type === 'bond') return 'ph:certificate-bold'
  return 'ph:coins-bold'
}

const INSTITUTION_LABELS: Record<string, string> = {
  bank_card: '银行卡',
  platform: '数字支付平台',
  broker: '证券券商',
  fund_platform: '基金平台',
  cash_wallet: '现金钱包',
  other: '其他账户',
}

// Global cached singletons
const accountMap = ref<Map<string | number, EntityDetail>>(new Map())
const assetMap = ref<Map<string | number, EntityDetail>>(new Map())
const nameToEntityLookup = ref<Map<string, EntityMatch>>(new Map())
const collisionMap = ref<Map<string, { asset?: EntityDetail; account?: EntityDetail }>>(new Map())
const isLoaded = ref(false)
const isLoading = ref(false)

export function useFinancialEntities() {
  async function loadEntities(force = false) {
    if (isLoading.value) return
    if (isLoaded.value && !force) return

    isLoading.value = true
    try {
      const [accRes, assetRes, holdingsRes] = await Promise.allSettled([
        accountsApi.list(),
        assetsApi.list({ page_size: 100 }),
        reportsApi.holdings(),
      ])

      const nextAccounts = new Map<string | number, EntityDetail>()
      const nextAssets = new Map<string | number, EntityDetail>()
      const nextLookup = new Map<string, EntityMatch>()
      const nextCollisions = new Map<string, { asset?: EntityDetail; account?: EntityDetail }>()

      // 1. Process accounts
      if (accRes.status === 'fulfilled' && Array.isArray(accRes.value)) {
        for (const a of accRes.value) {
          if (!a.name || isStopword(a.name)) continue
          const detail: EntityDetail = {
            id: a.id,
            name: a.name,
            type: 'account',
            icon: a.icon || 'ph:wallet-bold',
            institution: INSTITUTION_LABELS[a.institution] || a.institution || '账户容器',
            assetCount: a.asset_count || 0,
            currentValue: Number(a.total_value || 0),
          }
          nextAccounts.set(a.id, detail)
          nextLookup.set(a.name, { id: a.id, name: a.name, type: 'account' })
        }
      }

      // 2. Process holdings map for fast PnL lookup
      const holdingsMap = new Map<number, HoldingsItem>()
      if (holdingsRes.status === 'fulfilled' && holdingsRes.value?.holdings) {
        for (const h of holdingsRes.value.holdings) {
          holdingsMap.set(h.asset_id, h)
        }
      }

      // 3. Process assets
      if (assetRes.status === 'fulfilled' && Array.isArray(assetRes.value?.list)) {
        for (const a of assetRes.value.list) {
          if (!a.name || isStopword(a.name)) continue
          const holding = holdingsMap.get(a.id)
          const detail: EntityDetail = {
            id: a.id,
            name: a.name,
            type: 'asset',
            icon: getAssetIcon(a.asset_type),
            accountName: a.account_name || '默认账户',
            assetType: a.category_name || a.asset_type || '资产',
            currentValue: Number(a.current_value || 0),
            costBasis: Number(a.cost_basis || 0),
            quantity: Number(a.quantity || 0),
            floatingPnl: holding ? holding.floating_pnl : (a.current_value && a.cost_basis ? a.current_value - a.cost_basis : 0),
            floatingPct: holding ? holding.floating_pct : 0,
            currency: a.currency || 'CNY',
          }
          nextAssets.set(a.id, detail)

          // Check collision with account of identical name
          if (nextLookup.has(a.name)) {
            const existing = nextLookup.get(a.name)!
            if (existing.type === 'account') {
              const accDetail = nextAccounts.get(existing.id)
              nextCollisions.set(a.name, {
                asset: detail,
                account: accDetail,
              })
            }
          }

          // Asset takes primary precedence in match lookup
          nextLookup.set(a.name, { id: a.id, name: a.name, type: 'asset' })
        }
      }

      accountMap.value = nextAccounts
      assetMap.value = nextAssets
      nameToEntityLookup.value = nextLookup
      collisionMap.value = nextCollisions
      isLoaded.value = true
    } catch {
      // ignore
    } finally {
      isLoading.value = false
    }
  }

  function linkFinancialEntities(html: string): string {
    return replaceTextNodesWithEntities(html, nameToEntityLookup.value)
  }

  function getEntityDetail(type: 'asset' | 'account', id: string | number): EntityDetail | null {
    if (type === 'account') {
      return accountMap.value.get(id) || accountMap.value.get(Number(id)) || null
    }
    return assetMap.value.get(id) || assetMap.value.get(Number(id)) || null
  }

  function getCollisionEntities(name: string): { asset?: EntityDetail; account?: EntityDetail } | null {
    return collisionMap.value.get(name) || null
  }

  return {
    loadEntities,
    linkFinancialEntities,
    getEntityDetail,
    getCollisionEntities,
  }
}
