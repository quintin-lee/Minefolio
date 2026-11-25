<template>
  <div class="assets-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item active">{{ t('nav.assets') }}</button>
        <button class="segmented-item" @click="$router.push('/m/holdings')">{{ t('nav.holdings') }}</button>
      </div>
    </div>
    <div v-if="list.length === 0 && !loading" class="empty-state">暂无资产数据</div>
    <div v-for="group in groups" :key="group.key" class="account-group">
      <div class="account-header" @click="toggle(group.key)">
        <span class="account-avatar"><Icon :icon="group.icon" /></span>
        <div class="account-header-text">
          <div class="account-header-name">
            <span class="name">{{ group.label }}</span>
            <span v-if="group.institution" class="currency-tag">{{ group.institution }}</span>
          </div>
          <div class="account-header-sub">{{ group.assets.length }} 项资产</div>
        </div>
        <el-icon class="chevron" :class="{ collapsed: isCollapsed(group.key) }">
          <ArrowUp />
        </el-icon>
      </div>
      <div v-if="!isCollapsed(group.key)" class="account-body">
        <div v-for="a in group.assets" :key="a.id" class="asset-card">
          <div class="asset-info">
            <span class="name">{{ a.name }}</span>
            <span v-if="a.currency && a.currency !== 'CNY'" class="currency-tag">{{ a.currency }}</span>
          </div>
          <div class="value-col">
            <span class="value">{{ formatCurrency(a.current_value, a.currency) }}</span>
            <span v-if="a.currency && a.currency !== 'CNY' && exchangeRates[a.currency]" class="cny-hint">
              ≈ ¥{{ (Number(a.current_value) * (exchangeRates[a.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
            </span>
          </div>
        </div>
        <div v-if="group.assets.length === 0" class="group-empty">暂无资产</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ArrowUp } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { marketApi } from '@/api/market'
import { useLedgerStore } from '@/stores/ledger'
import { query, rowsFrom } from '@/db/local'
import { formatCurrency } from '@/utils/format'
import { t } from '@/utils/locale'
import type { Asset, Account } from '@/types'

const ledgerStore = useLedgerStore()
const list = ref<Asset[]>([])
const accounts = ref<Account[]>([])
const loading = ref(false)
const exchangeRates = ref<Record<string, number>>({})
const collapsed = ref<Set<string>>(new Set())

const institutionLabels: Record<string, string> = {
  bank_card: '银行卡',
  platform: '平台账户',
  broker: '券商',
  fund_platform: '基金平台',
  cash_wallet: '现金钱包',
  other: '其他',
}

const groups = computed(() => {
  type Group = { key: string; label: string; icon: string; institution: string; account?: Account; assets: Asset[] }
  const byKey = new Map<string, Group>()
  for (const acc of accounts.value) {
    byKey.set(`acct-${acc.id}`, {
      key: `acct-${acc.id}`,
      label: acc.name,
      icon: acc.icon || 'ph:wallet',
      institution: institutionLabels[acc.institution] || acc.institution,
      account: acc,
      assets: [],
    })
  }
  for (const a of list.value) {
    const key = a.account_id && a.account_id > 0 ? `acct-${a.account_id}` : 'ungrouped'
    let g = byKey.get(key)
    if (!g) {
      g = { key: 'ungrouped', label: '未分组', icon: 'ph:wallet', institution: '', assets: [] }
      byKey.set(key, g)
    }
    g.assets.push(a)
  }
  const out = Array.from(byKey.values())
  out.sort((x, y) => {
    if (x.key === 'ungrouped') return 1
    if (y.key === 'ungrouped') return -1
    const xa = x.account
    const ya = y.account
    if (!xa && !ya) return 0
    if (!xa) return 1
    if (!ya) return -1
    return (xa.sort_order - ya.sort_order) || (xa.id - ya.id)
  })
  return out
})

function isCollapsed(key: string) {
  return collapsed.value.has(key)
}

function toggle(key: string) {
  if (collapsed.value.has(key)) {
    collapsed.value.delete(key)
  } else {
    collapsed.value.add(key)
  }
}

onMounted(async () => {
  loading.value = true
  try {
    const currentLedgerId = ledgerStore.currentLedgerId || 0
    const [accountsRes, assetsRes, ratesRes] = await Promise.allSettled([
      accountsApi.list(),
      assetsApi.list({ page_size: 500 }),
      marketApi.getExchangeRates()
    ])
    if (accountsRes.status === 'fulfilled') {
      accounts.value = accountsRes.value
    } else {
      try {
        accounts.value = rowsFrom(query(
          'SELECT * FROM accounts WHERE __deleted = 0 AND archived = 0 AND (ledger_id = ? OR ledger_id = 0) ORDER BY sort_order, id',
          [currentLedgerId]
        )) as unknown as Account[]
      } catch {
        /* localDb unavailable */
      }
    }
    if (assetsRes.status === 'fulfilled') {
      list.value = assetsRes.value.list
    } else {
      try {
        list.value = rowsFrom(query(
          'SELECT * FROM assets WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id = 0) ORDER BY id',
          [currentLedgerId]
        )) as unknown as Asset[]
      } catch {
        /* localDb unavailable */
      }
    }
    if (ratesRes.status === 'fulfilled' && ratesRes.value) {
      exchangeRates.value = ratesRes.value
    }
  } catch (e) {
    console.error('[AssetsMobile] load error:', e)
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.assets-mobile {
  padding-bottom: 8px;
}
.account-group {
  margin-bottom: 12px;
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  overflow: hidden;
  background: var(--mf-surface);
}
.account-header {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px 14px;
  background: var(--mf-surface-muted);
  cursor: pointer;
}
.account-avatar {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: 8px;
  background: var(--mf-info-light);
  color: var(--mf-info);
  font-size: 18px;
  flex-shrink: 0;
}
.account-header-text {
  flex: 1;
  min-width: 0;
}
.account-header-name {
  display: flex;
  align-items: center;
  gap: 6px;
}
.account-header-sub {
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-top: 2px;
}
.chevron {
  color: var(--mf-text-muted);
  transition: transform 0.2s;
}
.chevron.collapsed {
  transform: rotate(180deg);
}
.account-body {
  padding: 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 70vh;
  max-height: 70dvh;
  overflow-y: auto;
  -webkit-overflow-scrolling: touch;
  overscroll-behavior-y: auto;
}
.group-empty {
  text-align: center;
  color: var(--mf-text-muted);
  font-size: 12px;
  padding: 16px 0;
}
.asset-card {
  flex-shrink: 0;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  padding: 12px;
}
.asset-info {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  flex: 1;
}
.name {
  font-weight: 500;
  font-size: 15px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.currency-tag {
  flex-shrink: 0;
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 4px;
  background: var(--mf-info-light);
  color: var(--mf-info);
  border: 1px solid var(--mf-info-border);
}
.value-col {
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  text-align: right;
  gap: 2px;
}
.value {
  font-family: 'JetBrains Mono', monospace;
  font-size: 15px;
}
.cny-hint {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: 'JetBrains Mono', monospace;
}
.empty-state {
  text-align: center;
  color: var(--mf-text-muted);
  padding: 40px 0;
  font-size: 14px;
}
</style>
