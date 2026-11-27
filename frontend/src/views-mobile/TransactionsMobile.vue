<template>
  <div class="tx-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item" @click="$router.push('/m/expenses')">{{ t('nav.dailyExpenses') }}</button>
        <button class="segmented-item active">{{ t('nav.transactions') }}</button>
      </div>
    </div>
    <div class="fab-row">
      <el-button type="primary" size="small" @click="openTransaction()">新增交易</el-button>
    </div>
    <div v-if="list.length === 0 && !loading" class="empty-state">暂无交易记录</div>
    <div v-for="t in list" :key="t.id" class="tx-card">
      <div class="top"><span>{{ t.category_name || t.transaction_type }}</span><span :class="isIncome(t) ? 'income' : 'expense'">{{ isIncome(t) ? '+' : '-' }}{{ formatCurrency(t.amount, t.currency) }}</span></div>
      <div class="bottom">
        <span>{{ t.transaction_date }}</span><span>{{ t.asset_name }}</span>
        <span class="row-actions">
          <el-button size="small" text type="primary" @click="openTransaction(t)">编辑</el-button>
          <el-button size="small" text type="danger" @click="removeTransaction(t)">删除</el-button>
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
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { transactionsApi } from '@/api/transactions'
import { useLedgerStore } from '@/stores/ledger'
import { formatCurrency, isIncomeTransaction } from '@/utils/format'
import { t } from '@/utils/locale'
import { query, rowsFrom } from '@/db/local'
import { offlineApi } from '@/utils/offline-http'
import TransactionSheet from '@/components/mobile/TransactionSheet.vue'
import type { Transaction, Asset, Category } from '@/types'

const PAGE_SIZE = 30
const list = ref<Transaction[]>([])
const loading = ref(false)
const page = ref(1)
const hasMore = ref(true)
const ledgerStore = useLedgerStore()
const assets = ref<Asset[]>([])
const categories = ref<Category[]>([])
const sheetVisible = ref(false)
const editing = ref<Transaction | null>(null)
function isIncome(t: Transaction) { return isIncomeTransaction(t) }

function loadOptions() {
  try {
    assets.value = rowsFrom(query(
      'SELECT * FROM assets WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id = 0) ORDER BY id',
      [ledgerStore.currentLedgerId || 0]
    )) as unknown as Asset[]
    categories.value = rowsFrom(query('SELECT * FROM categories WHERE __deleted = 0 ORDER BY sort_order, id')) as unknown as Category[]
  } catch {
    assets.value = []
    categories.value = []
  }
}

function openTransaction(record: Transaction | null = null) {
  loadOptions()
  editing.value = record
  sheetVisible.value = true
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
  const res = query(`
    SELECT t.*, c.name AS category_name, a.name AS asset_name
    FROM transactions t
    LEFT JOIN categories c ON c.id = t.category_id
    LEFT JOIN assets a ON a.id = t.asset_id
    WHERE t.__deleted = 0 AND (t.ledger_id = ? OR t.ledger_id = 0)
    ORDER BY t.transaction_date DESC, t.id DESC
    LIMIT ? OFFSET ?
  `, [currentLedgerId, PAGE_SIZE, offset])
  const rows = rowsFrom(res) as unknown as Transaction[]
  list.value = reset ? rows : [...list.value, ...rows]
  hasMore.value = rows.length >= PAGE_SIZE
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  try {
    const res = await transactionsApi.list({ page: page.value, page_size: PAGE_SIZE })
    list.value = reset ? res.list : [...list.value, ...res.list]
    const total = Number(res.total || 0)
    hasMore.value = total > 0 ? list.value.length < total : res.list.length >= PAGE_SIZE
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

onMounted(() => loadData(true))
</script>

<style scoped>
.tx-mobile { padding-bottom: 8px; }
.fab-row { display: flex; margin-bottom: 12px; }
.fab-row .el-button { width: 100%; }
.row-actions { display: flex; flex-shrink: 0; margin-left: auto; }
.page-header { margin-bottom: 12px; }
.empty-state { text-align: center; padding: 36px 0; color: var(--mf-text-muted); font-size: 13px; }
.tx-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.tx-card .top { display: flex; justify-content: space-between; align-items: center; gap: 8px; font-size: 16px; }
.tx-card .top span:first-child { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tx-card .bottom { display: flex; justify-content: space-between; align-items: center; gap: 8px; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: var(--mf-income); flex-shrink: 0; white-space: nowrap; } .expense { color: var(--mf-expense); flex-shrink: 0; white-space: nowrap; }
.load-more-wrap { display: flex; justify-content: center; margin: 16px 0 24px; }
.load-more-btn { width: 100%; border-radius: var(--mf-radius-md); color: var(--mf-text-muted); }
</style>
