<template>
  <div class="dashboard-mobile" v-loading="loading">
    <div class="page-header">
      <div class="brand-title">
        <AppLogo :size="24" :with-text="false" />
        <h2>首页</h2>
      </div>
    </div>
    <button class="ai-entry-card" @click="$router.push('/m/chat')">
      <Icon icon="ph:sparkle-fill" class="ai-entry-icon" />
      <div class="ai-entry-text">
        <b>AI 助手</b>
        <span>智能问答 · 账单解析 · 投资分析</span>
      </div>
      <Icon icon="ph:arrow-right" class="ai-entry-arrow" />
    </button>
    <div class="kpi-row">
      <div class="kpi-card cyan"><span>总资产</span><b>{{ formatCurrency(summary.total_assets) }}</b></div>
      <div class="kpi-card red"><span>总负债</span><b>{{ formatCurrency(Math.abs(summary.total_liabilities)) }}</b></div>
      <div class="kpi-card green"><span>净资产</span><b>{{ formatCurrency(summary.net_worth) }}</b></div>
    </div>
    <h3 class="section-title">本月收支</h3>
    <div class="mini-row">
      <div><span>收入</span><b class="income">{{ formatCurrency(month?.total_income ?? 0) }}</b></div>
      <div><span>支出</span><b class="expense">{{ formatCurrency(month?.total_expense ?? 0) }}</b></div>
      <div><span>结余</span><b>{{ formatCurrency(month?.balance ?? 0) }}</b></div>
    </div>
    <h3 class="section-title">最近记录</h3>
    <div v-if="recent.length === 0 && !loading" class="empty-state">暂无交易记录</div>
    <div v-for="e in recent" :key="e.id" class="record-card">
      <span class="cat">{{ e.category_name }}</span>
      <span :class="e.expense_type === 'income' ? 'income' : 'expense'">
        {{ e.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(e.amount, e.currency) }}
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { useLedgerStore } from '@/stores/ledger'
import { query, rowsFrom } from '@/db/local'
import { formatCurrency } from '@/utils/format'
import AppLogo from '@/components/AppLogo.vue'
import type { Summary, DailyExpense, ExpenseMonthly } from '@/types'

const ledgerStore = useLedgerStore()
const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, category_breakdown: [], trend: [] })
const month = ref<ExpenseMonthly | null>(null)
const recent = ref<DailyExpense[]>([])
const loading = ref(false)

onMounted(async () => {
  loading.value = true
  try {
    const now = new Date()
    const currentLedgerId = ledgerStore.currentLedgerId || 0
    const [sRes, mRes, rRes] = await Promise.allSettled([
      summaryApi.get(),
      dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
      dailyExpensesApi.list({ page_size: 5 }),
    ])
    if (sRes.status === 'fulfilled' && sRes.value) {
      summary.value = sRes.value
    }
    if (mRes.status === 'fulfilled' && mRes.value) {
      month.value = mRes.value
    } else {
      try {
        const prefix = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}`
        const rows = rowsFrom(query(
          `SELECT expense_type, SUM(amount) AS total
             FROM daily_expenses
            WHERE __deleted = 0 AND (ledger_id = ? OR ledger_id = 0) AND substr(expense_date, 1, 7) = ?
            GROUP BY expense_type`,
          [currentLedgerId, prefix]
        ))
        let totalIncome = 0
        let totalExpense = 0
        for (const r of rows) {
          if (r.expense_type === 'income') totalIncome = Number(r.total) || 0
          else if (r.expense_type === 'expense') totalExpense = Number(r.total) || 0
        }
        month.value = { total_income: totalIncome, total_expense: totalExpense, balance: totalIncome - totalExpense } as unknown as ExpenseMonthly
      } catch {
        /* localDb unavailable */
      }
    }
    if (rRes.status === 'fulfilled' && rRes.value) {
      recent.value = rRes.value.list
    } else {
      try {
        const rows = rowsFrom(query(
          `SELECT e.*, c.name AS category_name
             FROM daily_expenses e
             LEFT JOIN categories c ON c.id = e.category_id
            WHERE e.__deleted = 0 AND (e.ledger_id = ? OR e.ledger_id = 0)
            ORDER BY e.expense_date DESC, e.id DESC
            LIMIT 5`,
          [currentLedgerId]
        ))
        recent.value = rows as unknown as DailyExpense[]
      } catch {
        /* localDb unavailable */
      }
    }
  } catch (e) {
    console.error('[DashboardMobile] onMounted error:', e)
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.page-header { margin-bottom: 16px; }
.brand-title { display: flex; align-items: center; gap: 10px; }
.brand-title h2 { margin: 0; font-size: 20px; font-weight: 700; color: var(--mf-text-main); }
.ai-entry-card { display: flex; align-items: center; gap: 12px; width: 100%; padding: 14px 16px; margin-bottom: 16px; background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-accent) 100%); border: none; border-radius: 12px; color: #fff; cursor: pointer; box-shadow: var(--mf-shadow-sm); transition: transform 0.15s ease; }
.ai-entry-card:active { transform: scale(0.98); }
.ai-entry-icon { font-size: 28px; opacity: 0.9; }
.ai-entry-text { flex: 1; display: flex; flex-direction: column; gap: 2px; text-align: left; }
.ai-entry-text b { font-size: 15px; font-weight: 700; }
.ai-entry-text span { font-size: 11px; opacity: 0.8; }
.ai-entry-arrow { font-size: 18px; opacity: 0.7; }
.kpi-row { display: flex; gap: 12px; overflow-x: auto; }
.kpi-card { flex: 0 0 120px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; display: flex; flex-direction: column; gap: 8px; box-shadow: var(--mf-shadow-sm); }
.kpi-card.cyan b { color: var(--mf-info); } .kpi-card.red b { color: var(--mf-danger); } .kpi-card.green b { color: var(--mf-success); }
.kpi-card span { color: var(--mf-text-muted); font-size: 12px; }
.kpi-card b { font-size: 18px; font-family: 'JetBrains Mono', monospace; }
.mini-row { display: flex; justify-content: space-between; margin: 12px 0; }
.mini-row > div { display: flex; flex-direction: column; gap: 4px; }
.mini-row span { color: var(--mf-text-muted); font-size: 12px; }
.mini-row b { font-family: 'JetBrains Mono', monospace; }
.section-title { margin: 16px 0 8px; font-size: 14px; color: var(--mf-text-muted); }
.record-card { display: flex; justify-content: space-between; padding: 12px; border-bottom: 1px solid var(--mf-border); }
.income { color: var(--mf-gain); } .expense { color: var(--mf-loss); }
.empty-state { text-align: center; color: var(--mf-text-muted); padding: 20px 0; font-size: 13px; }
</style>
