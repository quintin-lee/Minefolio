<template>
  <div class="assets-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item active">{{ t('nav.assets') }}</button>
        <button class="segmented-item" @click="$router.push('/m/holdings')">{{ t('nav.holdings') }}</button>
      </div>
    </div>
    <div v-if="list.length === 0 && !loading" class="empty-state">暂无资产数据</div>
    <div class="fab-row">
      <el-button plain @click="openAccount()">新增账户</el-button>
      <el-button type="primary" @click="openAsset()">新增资产</el-button>
    </div>
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
        <span class="group-actions" @click.stop>
          <el-button v-if="group.account" text type="primary" @click="openAccount(group.account)">编辑</el-button>
          <el-button v-if="group.account" text type="danger" @click="removeAccount(group.account)">删除</el-button>
        </span>
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
          <div class="asset-actions" @click.stop>
            <el-button text type="primary" @click="openAsset(a)">编辑</el-button>
            <el-button text type="danger" @click="removeAsset(a)">删除</el-button>
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
    <div v-if="accountTotal > accountPageSize" class="mobile-pager">
      <el-button :disabled="accountPage <= 1" @click="changeAccountPage(accountPage - 1)">上一页</el-button>
      <span class="pager-info">{{ accountPage }} / {{ Math.max(1, Math.ceil(accountTotal / accountPageSize)) }}（共 {{ accountTotal }} 户）</span>
      <el-button :disabled="accountPage * accountPageSize >= accountTotal" @click="changeAccountPage(accountPage + 1)">下一页</el-button>
    </div>
    <AssetSheet
      v-model="assetSheetVisible"
      :record="editingAsset"
      :categories="categories"
      :accounts="allAccounts.length ? allAccounts : accounts"
      @saved="load"
    />
    <AccountSheet v-model="accountSheetVisible" :record="editingAccount" @saved="load" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { ArrowUp } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { marketApi } from '@/api/market'
import { useLedgerStore } from '@/stores/ledger'
import { query, rowsFrom } from '@/db/local'
import { offlineApi } from '@/utils/offline-http'
import { formatCurrency } from '@/utils/format'
import { t } from '@/utils/locale'
import AssetSheet from '@/components/mobile/AssetSheet.vue'
import AccountSheet from '@/components/mobile/AccountSheet.vue'
import { fetchAllPages } from '@/utils/pagination'
import type { Asset, Account, Category } from '@/types'

const ledgerStore = useLedgerStore()
const list = ref<Asset[]>([])
const accounts = ref<Account[]>([])
const allAccounts = ref<Account[]>([])
const accountPage = ref(1)
const accountPageSize = ref(20)
const accountTotal = ref(0)
const loading = ref(false)
const exchangeRates = ref<Record<string, number>>({})
const expanded = ref<Set<string>>(new Set())
const categories = ref<Category[]>([])
const assetSheetVisible = ref(false)
const accountSheetVisible = ref(false)
const editingAsset = ref<Asset | null>(null)
const editingAccount = ref<Account | null>(null)

function openAsset(asset: Asset | null = null) {
  editingAsset.value = asset
  assetSheetVisible.value = true
}
function openAccount(account: Account | null = null) {
  editingAccount.value = account
  accountSheetVisible.value = true
}

async function removeAsset(asset: Asset) {
  try {
    await ElMessageBox.confirm(`删除资产「${asset.name}」？`, '确认删除', { type: 'warning' })
    await offlineApi.delete(`/assets/${asset.id}`)
    await load()
  } catch (err: any) {
    if (err !== 'cancel' && err !== 'close') ElMessage.error(String(err?.response?.data?.message || err?.message || '删除失败'))
  }
}

async function removeAccount(account: Account) {
  try {
    await ElMessageBox.confirm(`删除账户「${account.name}」？`, '确认删除', { type: 'warning' })
    await offlineApi.delete(`/accounts/${account.id}`)
    await load()
  } catch (err: any) {
    if (err !== 'cancel' && err !== 'close') ElMessage.error(String(err?.response?.data?.message || err?.message || '删除失败'))
  }
}

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
    const hasAccountId = Boolean(a.account_id && a.account_id > 0)
    if (hasAccountId && !byKey.has(`acct-${a.account_id}`)) {
      continue
    }
    const key = hasAccountId ? `acct-${a.account_id}` : 'ungrouped'
    if (key === 'ungrouped' && accountPage.value > 1) {
      continue
    }
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
  return !expanded.value.has(key)
}

function toggle(key: string) {
  if (expanded.value.has(key)) {
    expanded.value.delete(key)
  } else {
    expanded.value.add(key)
  }
}

async function load() {
  loading.value = true
  try {
    const currentLedgerId = ledgerStore.currentLedgerId || 0
    try {
      categories.value = rowsFrom(query('SELECT * FROM categories WHERE __deleted = 0 ORDER BY sort_order, id')) as unknown as Category[]
    } catch {
      categories.value = []
    }
    const [accountsRes, assetsRes, ratesRes] = await Promise.allSettled([
      accountsApi.list({ page: accountPage.value, page_size: accountPageSize.value }),
      assetsApi.list({ page_size: 500 }),
      marketApi.getExchangeRates()
    ])
    if (accountsRes.status === 'fulfilled') {
      const val: any = accountsRes.value
      const accList: Account[] = Array.isArray(val) ? val : (Array.isArray(val?.list) ? val.list : [])
      const accTotal = Array.isArray(val) ? val.length : (typeof val?.total === 'number' ? val.total : accList.length)
      accounts.value = accList
      accountTotal.value = accTotal
      if (accTotal <= accList.length && accountPage.value === 1) {
        allAccounts.value = accList
      } else if (allAccounts.value.length === 0) {
        fetchAllPages((p, ps) => accountsApi.list({ page: p, page_size: ps }))
          .then(all => { allAccounts.value = all })
          .catch(() => {})
      }
    } else {
      try {
        const localAccounts = rowsFrom(query(
          'SELECT * FROM accounts WHERE __deleted = 0 AND archived = 0 AND (ledger_id = ? OR ledger_id = 0) ORDER BY sort_order, id',
          [currentLedgerId]
        )) as unknown as Account[]
        allAccounts.value = localAccounts
        accountTotal.value = localAccounts.length
        const start = (accountPage.value - 1) * accountPageSize.value
        accounts.value = localAccounts.slice(start, start + accountPageSize.value)
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
}

function changeAccountPage(nextPage: number) {
  accountPage.value = nextPage
  load()
}

onMounted(load)
</script>

<style scoped>
.assets-mobile {
  padding-bottom: 8px;
}
.fab-row { display: flex; gap: 8px; margin-bottom: 12px; }
.fab-row .el-button { flex: 1; }
.asset-actions { display: flex; flex-shrink: 0; margin-right: 8px; }
.group-actions { display: flex; flex-shrink: 0; margin-left: auto; }
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
.mobile-pager {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 12px 0;
}
.pager-info {
  font-size: 12px;
  color: var(--mf-text-muted);
}
</style>
