<template>
  <div class="tx-mobile" v-loading="loading">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item" @click="$router.push('/m/expenses')">{{ t('nav.dailyExpenses') }}</button>
        <button class="segmented-item active">{{ t('nav.transactions') }}</button>
      </div>
    </div>
    <div v-if="list.length === 0 && !loading" class="empty-state">暂无交易记录</div>
    <div v-for="t in list" :key="t.id" class="tx-card">
      <div class="top"><span>{{ t.category_name || t.transaction_type }}</span><span :class="isIncome(t) ? 'income' : 'expense'">{{ isIncome(t) ? '+' : '-' }}{{ formatCurrency(t.amount, t.currency) }}</span></div>
      <div class="bottom"><span>{{ t.transaction_date }}</span><span>{{ t.asset_name }}</span></div>
    </div>
    <div v-if="list.length > 0 && hasMore" class="load-more-wrap">
      <el-button plain class="load-more-btn" :loading="loading" @click="loadMore">
        加载更多
      </el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { transactionsApi } from '@/api/transactions'
import { useLedgerStore } from '@/stores/ledger'
import { formatCurrency, isIncomeTransaction } from '@/utils/format'
import { t } from '@/utils/locale'
import { query, rowsFrom } from '@/db/local'
import type { Transaction } from '@/types'

const PAGE_SIZE = 30
const list = ref<Transaction[]>([])
const loading = ref(false)
const page = ref(1)
const hasMore = ref(true)
const ledgerStore = useLedgerStore()
function isIncome(t: Transaction) { return isIncomeTransaction(t) }

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
.tx-mobile { padding-bottom: 24px; }
.page-header { margin-bottom: 12px; }
.empty-state { text-align: center; padding: 36px 0; color: var(--mf-text-muted); font-size: 13px; }
.tx-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; }
.tx-card .top { display: flex; justify-content: space-between; font-size: 16px; }
.tx-card .bottom { display: flex; justify-content: space-between; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.income { color: var(--mf-gain); } .expense { color: var(--mf-loss); }
.load-more-wrap { display: flex; justify-content: center; margin: 16px 0 24px; }
.load-more-btn { width: 100%; border-radius: var(--mf-radius-md); color: var(--mf-text-muted); }
</style>
