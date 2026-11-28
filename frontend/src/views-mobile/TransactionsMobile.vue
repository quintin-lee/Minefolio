<template>
  <div class="tx-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item" @click="$router.push('/m/expenses')">{{ t('nav.dailyExpenses') }}</button>
        <button class="segmented-item active">{{ t('nav.transactions') }}</button>
      </div>
    </div>

    <!-- 本月交易概览 -->
    <div v-if="monthSummary" class="summary-strip-card">
      <div class="strip-col">
        <span class="strip-lbl">本月笔数</span>
        <span class="strip-val">{{ monthSummary.count }} 笔</span>
      </div>
      <div class="strip-col">
        <span class="strip-lbl">资金流入</span>
        <span class="strip-val income mf-amount-mask">{{ formatCurrency(monthSummary.inflows) }}</span>
      </div>
      <div class="strip-col">
        <span class="strip-lbl">资金流出</span>
        <span class="strip-val expense mf-amount-mask">{{ formatCurrency(monthSummary.outflows) }}</span>
      </div>
    </div>

    <div class="fab-row">
      <el-button type="primary" @click="openTransaction()">新增交易</el-button>
    </div>

    <!-- 紧凑筛选栏 -->
    <div class="filter-bar">
      <div class="chip-scroll">
        <button
          v-for="opt in quickTypeOptions"
          :key="opt.value"
          type="button"
          class="filter-chip"
          :class="{ active: filters.type === opt.value }"
          @click="selectQuickType(opt.value)"
        >
          {{ opt.label }}
        </button>
      </div>
      <el-button plain class="filter-trigger-btn" @click="openFilterDrawer">
        <Icon icon="ph:funnel-simple" style="margin-right: 4px" />
        筛选<span v-if="activeFilterCount > 0" class="filter-count">{{ activeFilterCount }}</span>
      </el-button>
    </div>

    <el-empty v-if="list.length === 0 && !loading" description="暂无交易记录" :image-size="72">
      <el-button type="primary" @click="openTransaction()">记一笔交易</el-button>
    </el-empty>
    <div v-for="item in list" :key="item.id" class="tx-card">
      <div class="top">
        <span>{{ item.category_name || typeLabel(item.transaction_type) }}</span>
        <span :class="[isIncome(item) ? 'income' : 'expense', 'mf-amount-mask']">
          {{ isIncome(item) ? '+' : '-' }}{{ formatCurrency(item.amount, item.currency) }}
        </span>
      </div>
      <div class="bottom">
        <span>{{ item.transaction_date }}</span>
        <span>{{ item.asset_name }}</span>
        <span class="row-actions">
          <el-button text type="primary" @click="openTransaction(item)">编辑</el-button>
          <el-button text type="danger" @click="removeTransaction(item)">删除</el-button>
        </span>
      </div>
    </div>
    <div v-if="list.length > 0 && hasMore" class="load-more-wrap">
      <el-button plain class="load-more-btn" :loading="loading" @click="loadMore">
        加载更多
      </el-button>
    </div>
    <TransactionSheet
      v-model="sheetVisible"
      :record="editing"
      :assets="assets"
      :categories="categories"
      @saved="refresh"
    />

    <el-drawer
      v-model="filterDrawer"
      direction="btt"
      size="auto"
      title="筛选交易流水"
      class="mobile-filter-drawer"
    >
      <div class="drawer-form">
        <div class="drawer-field">
          <label>交易类型</label>
          <el-select v-model="filters.type" placeholder="全部类型" clearable style="width: 100%">
            <el-option v-for="opt in allTransactionTypes" :key="opt.value" :label="opt.label" :value="opt.value" />
          </el-select>
        </div>
        <div class="drawer-field">
          <label>资产账户</label>
          <el-select v-model="filters.asset_id" placeholder="全部资产" clearable style="width: 100%">
            <el-option v-for="a in assets" :key="a.id" :label="assetLabel(a)" :value="String(a.id)" />
          </el-select>
        </div>
        <div class="drawer-date-row">
          <div class="drawer-field">
            <label>开始日期</label>
            <el-date-picker
              v-model="filters.start_date"
              type="date"
              value-format="YYYY-MM-DD"
              placeholder="起始"
              clearable
              :editable="false"
              style="width: 100%"
            />
          </div>
          <div class="drawer-field">
            <label>结束日期</label>
            <el-date-picker
              v-model="filters.end_date"
              type="date"
              value-format="YYYY-MM-DD"
              placeholder="截止"
              clearable
              :editable="false"
              style="width: 100%"
            />
          </div>
        </div>
        <div class="drawer-actions">
          <el-button @click="resetFilters">重置</el-button>
          <el-button type="primary" @click="applyFilters">应用筛选</el-button>
        </div>
      </div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { useLedgerStore } from '@/stores/ledger'
import { formatCurrency, isIncomeTransaction, localThisMonth, assetLabel } from '@/utils/format'
import { t } from '@/utils/locale'
import { query, rowsFrom } from '@/db/local'
import { offlineApi } from '@/utils/offline-http'
import TransactionSheet from '@/components/mobile/TransactionSheet.vue'
import type { Transaction, TransactionMonthly, Asset, Category } from '@/types'

const PAGE_SIZE = 30
const list = ref<Transaction[]>([])
const monthSummary = ref<TransactionMonthly | null>(null)
const loading = ref(false)
const page = ref(1)
const hasMore = ref(true)
const ledgerStore = useLedgerStore()
const assets = ref<Asset[]>([])
const categories = ref<Category[]>([])
const sheetVisible = ref(false)
const filterDrawer = ref(false)
const editing = ref<Transaction | null>(null)

const filters = reactive({
  type: '',
  asset_id: '',
  start_date: '',
  end_date: '',
})

const quickTypeOptions = [
  { label: '全部', value: '' },
  { label: '买入', value: 'buy' },
  { label: '卖出', value: 'sell' },
  { label: '转出', value: 'transfer_out' },
  { label: '收益', value: 'income' },
]

const allTransactionTypes = [
  { value: 'deposit', label: '存入' },
  { value: 'withdrawal', label: '取出' },
  { value: 'buy', label: '买入' },
  { value: 'sell', label: '卖出' },
  { value: 'transfer_in', label: '转入' },
  { value: 'transfer_out', label: '转出' },
  { value: 'fee', label: '手续费' },
  { value: 'income', label: '收益' },
  { value: 'loss', label: '亏损' },
  { value: 'interest', label: '利息' },
]

const TYPE_LABEL_MAP: Record<string, string> = Object.fromEntries(allTransactionTypes.map((x) => [x.value, x.label]))

function typeLabel(tp: string): string {
  return TYPE_LABEL_MAP[tp] ?? tp
}

const activeFilterCount = computed(() => {
  let count = 0
  if (filters.type) count++
  if (filters.asset_id) count++
  if (filters.start_date || filters.end_date) count++
  return count
})

function isIncome(item: Transaction) { return isIncomeTransaction(item) }

function loadOptions() {
  try {
    assets.value = rowsFrom(query(
      'SELECT * FROM assets WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id = 0) ORDER BY id',
      [ledgerStore.currentLedgerId || 0],
    )) as unknown as Asset[]
    categories.value = rowsFrom(query('SELECT * FROM categories WHERE __deleted = 0 ORDER BY sort_order, id')) as unknown as Category[]
  } catch {
    assets.value = []
    categories.value = []
  }
  if (assets.value.length === 0 && typeof assetsApi.list === 'function') {
    assetsApi.list({ page_size: 200 }).then((res) => {
      if (res?.list?.length) assets.value = res.list
    }).catch(() => {})
  }
}

function openTransaction(record: Transaction | null = null) {
  loadOptions()
  editing.value = record
  sheetVisible.value = true
}

function openFilterDrawer() {
  loadOptions()
  filterDrawer.value = true
}

function selectQuickType(val: string) {
  filters.type = val
  loadData(true)
}

function resetFilters() {
  filters.type = ''
  filters.asset_id = ''
  filters.start_date = ''
  filters.end_date = ''
  filterDrawer.value = false
  loadData(true)
}

function applyFilters() {
  filterDrawer.value = false
  loadData(true)
}

async function removeTransaction(record: Transaction) {
  try {
    await ElMessageBox.confirm('删除这条交易记录？', '确认删除', { type: 'warning' })
    await offlineApi.delete(`/transactions/${record.id}`)
    await refresh()
  } catch (err: any) {
    if (err !== 'cancel' && err !== 'close') ElMessage.error(String(err?.response?.data?.message || err?.message || '删除失败'))
  }
}

function refresh() {
  page.value = 1
  return loadData(true)
}

function loadLocalList(reset: boolean) {
  const currentLedgerId = ledgerStore.currentLedgerId || 0
  const offset = (page.value - 1) * PAGE_SIZE
  const where: string[] = ['t.__deleted = 0', '(t.ledger_id = ? OR t.ledger_id = 0)']
  const params: any[] = [currentLedgerId]

  if (filters.type) {
    where.push('t.transaction_type = ?')
    params.push(filters.type)
  }
  if (filters.asset_id) {
    where.push('t.asset_id = ?')
    params.push(Number(filters.asset_id))
  }
  if (filters.start_date) {
    where.push('t.transaction_date >= ?')
    params.push(filters.start_date)
  }
  if (filters.end_date) {
    where.push('t.transaction_date <= ?')
    params.push(filters.end_date)
  }
  params.push(PAGE_SIZE, offset)

  const res = query(`
    SELECT t.*, c.name AS category_name, a.name AS asset_name
    FROM transactions t
    LEFT JOIN categories c ON c.id = t.category_id
    LEFT JOIN assets a ON a.id = t.asset_id
    WHERE ${where.join(' AND ')}
    ORDER BY t.transaction_date DESC, t.id DESC
    LIMIT ? OFFSET ?
  `, params)
  const rows = rowsFrom(res) as unknown as Transaction[]
  list.value = reset ? rows : [...list.value, ...rows]
  hasMore.value = rows.length >= PAGE_SIZE
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  try {
    const params: Record<string, any> = { page: page.value, page_size: PAGE_SIZE }
    if (filters.type) params.type = filters.type
    if (filters.asset_id) params.asset_id = filters.asset_id
    if (filters.start_date) params.start_date = filters.start_date
    if (filters.end_date) params.end_date = filters.end_date

    const listPromise = transactionsApi.list(params)
    const monthlyPromise = reset && typeof transactionsApi.monthly === 'function'
      ? transactionsApi.monthly(localThisMonth())
      : Promise.resolve(null)

    const [listRes, monthlyRes] = await Promise.allSettled([listPromise, monthlyPromise])
    if (listRes.status === 'fulfilled') {
      list.value = reset ? listRes.value.list : [...list.value, ...listRes.value.list]
      const total = Number(listRes.value.total || 0)
      hasMore.value = total > 0 ? list.value.length < total : listRes.value.list.length >= PAGE_SIZE
    } else {
      try {
        loadLocalList(reset)
      } catch (e) {
        console.error('[TransactionsMobile] load failed:', e)
        hasMore.value = false
      }
    }
    if (monthlyRes.status === 'fulfilled' && monthlyRes.value) {
      monthSummary.value = monthlyRes.value
    }
  } catch {
    try {
      loadLocalList(reset)
    } catch (e) {
      console.error('[TransactionsMobile] load failed:', e)
      hasMore.value = false
    }
  } finally {
    loading.value = false
  }
}

function loadMore() {
  if (loading.value || !hasMore.value) return
  page.value++
  loadData()
}

onMounted(() => {
  loadOptions()
  loadData(true)
})
</script>

<style scoped>
.tx-mobile { padding-bottom: 8px; }
.summary-strip-card {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 12px;
  padding: 12px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
}
.strip-col { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
.strip-lbl { font-size: 11px; color: var(--mf-text-muted); }
.strip-val { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 600; color: var(--mf-text-main); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.fab-row { display: flex; margin-bottom: 10px; }
.fab-row .el-button { width: 100%; }
.filter-bar { display: flex; align-items: center; gap: 8px; margin-bottom: 12px; }
.chip-scroll { display: flex; align-items: center; gap: 6px; flex: 1; min-width: 0; overflow-x: auto; }
.filter-chip {
  flex-shrink: 0;
  padding: 5px 10px;
  border-radius: 999px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface);
  color: var(--mf-text-muted);
  font-size: 12px;
  cursor: pointer;
}
.filter-chip.active {
  color: var(--mf-primary);
  border-color: var(--mf-primary-border);
  background: var(--mf-primary-light);
  font-weight: 600;
}
.filter-trigger-btn { flex-shrink: 0; }
.filter-count {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 16px;
  height: 16px;
  padding: 0 4px;
  margin-left: 4px;
  border-radius: 999px;
  background: var(--mf-primary);
  color: #fff;
  font-size: 10px;
}
.row-actions { display: flex; flex-shrink: 0; margin-left: auto; }
.page-header { margin-bottom: 12px; }
.tx-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.tx-card .top { display: flex; justify-content: space-between; align-items: center; gap: 8px; font-size: 16px; }
.tx-card .top span:first-child { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tx-card .bottom { display: flex; justify-content: space-between; align-items: center; gap: 8px; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: var(--mf-income); flex-shrink: 0; white-space: nowrap; } .expense { color: var(--mf-expense); flex-shrink: 0; white-space: nowrap; }
.load-more-wrap { display: flex; justify-content: center; margin: 16px 0 24px; }
.load-more-btn { width: 100%; border-radius: var(--mf-radius-md); color: var(--mf-text-muted); }
.drawer-form { display: flex; flex-direction: column; gap: 12px; padding-bottom: 12px; }
.drawer-date-row { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.drawer-field { display: flex; flex-direction: column; gap: 6px; }
.drawer-field label { font-size: 12px; color: var(--mf-text-muted); }
.drawer-actions { display: flex; gap: 10px; margin-top: 6px; }
.drawer-actions .el-button { flex: 1; }
</style>
