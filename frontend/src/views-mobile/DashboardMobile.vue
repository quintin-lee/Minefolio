<template>
  <div class="dashboard-mobile" v-loading="loading">
    <div class="page-header">
      <div class="brand-title">
        <AppLogo :size="24" :with-text="false" />
        <h2>首页</h2>
      </div>
    </div>

    <div v-if="pendingDcaCount > 0" class="dca-alert-pill" @click="$router.push('/m/plans')">
      <span class="dca-dot" />
      <span>您有 <strong>{{ pendingDcaCount }}</strong> 项定投计划待执行</span>
      <Icon icon="ph:arrow-right" class="dca-arrow" />
    </div>

    <button class="ai-entry-card" @click="$router.push('/m/chat')">
      <Icon icon="ph:sparkle-fill" class="ai-entry-icon" />
      <div class="ai-entry-text">
        <b>AI 助手</b>
        <span>智能问答 · 账单解析 · 投资分析</span>
      </div>
      <Icon icon="ph:arrow-right" class="ai-entry-arrow" />
    </button>

    <div class="quick-actions-grid">
      <button type="button" class="quick-action-item" @click="openScanner">
        <Icon icon="ph:camera-duotone" class="qa-icon" />
        <span>扫票记账</span>
      </button>
      <button type="button" class="quick-action-item" @click="$router.push('/m/market')">
        <Icon icon="ph:chart-line-up-duotone" class="qa-icon" />
        <span>市场行情</span>
      </button>
      <button type="button" class="quick-action-item" @click="$router.push('/m/news')">
        <Icon icon="ph:newspaper-duotone" class="qa-icon" />
        <span>财经早报</span>
      </button>
      <button type="button" class="quick-action-item" @click="$router.push('/m/plans')">
        <Icon icon="ph:calendar-check-duotone" class="qa-icon" />
        <span>定投计划</span>
      </button>
    </div>

    <div class="kpi-row">
      <div class="kpi-card green hero-kpi"><span>净资产</span><b class="mf-amount-mask">{{ formatCurrency(summary.net_worth) }}</b></div>
      <div class="kpi-card cyan"><span>总资产</span><b class="mf-amount-mask">{{ formatCurrency(summary.total_assets) }}</b></div>
      <div class="kpi-card red"><span>总负债</span><b class="mf-amount-mask">{{ formatCurrency(Math.abs(summary.total_liabilities)) }}</b></div>
    </div>

    <h3 class="section-title">本月收支</h3>
    <div class="mini-row">
      <div><span>收入</span><b class="income mf-amount-mask">{{ formatCurrency(month?.total_income ?? 0) }}</b></div>
      <div><span>支出</span><b class="expense mf-amount-mask">{{ formatCurrency(month?.total_expense ?? 0) }}</b></div>
      <div><span>结余</span><b class="mf-amount-mask">{{ formatCurrency(month?.balance ?? 0) }}</b></div>
    </div>

    <div v-if="assetTrend?.labels?.length" class="chart-card">
      <div class="chart-card-head">
        <span>净资产走势（近30天）</span>
      </div>
      <div class="trend-chart-wrap">
        <AssetTrendLine :data="assetTrend" />
      </div>
    </div>

    <div v-if="assetBreakdownData.length > 0" class="chart-card">
      <div class="chart-card-head">
        <span>资产配置分布</span>
      </div>
      <div class="pie-chart-wrap">
        <AssetBreakdownPie :data="assetBreakdownData" />
      </div>
    </div>

    <h3 class="section-title">最近记录</h3>
    <div v-if="recent.length === 0 && !loading" class="empty-state">暂无交易记录</div>
    <div
      v-for="e in recent"
      :key="e.id"
      class="record-card"
      @click="$router.push('/m/expenses')"
    >
      <span class="cat">{{ e.category_name || '未分类' }}</span>
      <span :class="[e.expense_type === 'income' ? 'income' : 'expense', 'mf-amount-mask']">
        {{ e.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(e.amount, e.currency) }}
      </span>
    </div>

    <ReceiptScannerModal v-model="scannerOpen" @created="loadDashboard" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import { summaryApi } from '@/api/summary'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { dcaApi } from '@/api/dca'
import { reportsApi, type AssetTrend } from '@/api/reports'
import { useLedgerStore } from '@/stores/ledger'
import { query, rowsFrom } from '@/db/local'
import { loadViewCache, saveViewCache } from '@/composables/mobile/useViewCache'
import { formatCurrency } from '@/utils/format'
import AppLogo from '@/components/AppLogo.vue'
import AssetTrendLine from '@/components/AssetTrendLine.vue'
import AssetBreakdownPie from '@/components/AssetBreakdownPie.vue'
import ReceiptScannerModal from '@/components/ReceiptScannerModal.vue'
import type { Summary, DailyExpense, ExpenseMonthly } from '@/types'

const ledgerStore = useLedgerStore()
const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, category_breakdown: [], trend: [] })
const month = ref<ExpenseMonthly | null>(null)
const recent = ref<DailyExpense[]>([])
const pendingDcaCount = ref(0)
const assetTrend = ref<AssetTrend | null>(null)
const loading = ref(false)
const scannerOpen = ref(false)

const assetBreakdownData = computed(() => {
  const rows = (summary.value.category_breakdown ?? []).filter((r) => Number(r.value) > 0)
  const total = rows.reduce((sum, r) => sum + (Number(r.value) || 0), 0)
  return rows.map((r) => ({
    category_name: r.category || '未分类',
    value: Number(r.value) || 0,
    pct: total > 0 ? ((Number(r.value) || 0) / total) * 100 : 0,
  }))
})

function openScanner() {
  if (typeof navigator !== 'undefined' && navigator.onLine === false) {
    ElMessage.warning('扫票记账需要网络连接')
    return
  }
  scannerOpen.value = true
}

async function loadDashboard() {
  loading.value = true
  try {
    const now = new Date()
    const currentLedgerId = ledgerStore.currentLedgerId || 0
    const trendCacheKey = `dashboard_asset_trend:${currentLedgerId}`
    const cachedTrend = loadViewCache<AssetTrend>(trendCacheKey)
    if (cachedTrend.data) {
      assetTrend.value = cachedTrend.data
    }

    const [sRes, mRes, rRes, dcaRes, tRes] = await Promise.allSettled([
      summaryApi.get(),
      dailyExpensesApi.monthly(now.getFullYear(), now.getMonth() + 1),
      dailyExpensesApi.list({ page_size: 5 }),
      typeof dcaApi.listPendingExecutions === 'function' ? dcaApi.listPendingExecutions() : Promise.resolve([]),
      typeof reportsApi.assetTrend === 'function' ? reportsApi.assetTrend('30d') : Promise.resolve(null),
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
          [currentLedgerId, prefix],
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
          [currentLedgerId],
        ))
        recent.value = rows as unknown as DailyExpense[]
      } catch {
        /* localDb unavailable */
      }
    }
    if (dcaRes.status === 'fulfilled' && Array.isArray(dcaRes.value)) {
      pendingDcaCount.value = dcaRes.value.length
    }
    if (tRes.status === 'fulfilled' && tRes.value) {
      assetTrend.value = tRes.value
      saveViewCache(trendCacheKey, tRes.value)
    }
  } catch (e) {
    console.error('[DashboardMobile] onMounted error:', e)
  } finally {
    loading.value = false
  }
}

onMounted(loadDashboard)
</script>

<style scoped>
.page-header { margin-bottom: 14px; }
.brand-title { display: flex; align-items: center; gap: 10px; }
.brand-title h2 { margin: 0; font-size: 20px; font-weight: 700; color: var(--mf-text-main); }
.dca-alert-pill {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px;
  margin-bottom: 12px;
  border-radius: 10px;
  background: var(--mf-warning-light, rgba(245, 158, 11, 0.12));
  border: 1px solid var(--mf-warning-border, rgba(245, 158, 11, 0.35));
  color: var(--mf-text-main);
  font-size: 13px;
  cursor: pointer;
}
.dca-dot { width: 8px; height: 8px; border-radius: 50%; background: var(--mf-warning); flex-shrink: 0; }
.dca-arrow { margin-left: auto; color: var(--mf-text-muted); }
.ai-entry-card { display: flex; align-items: center; gap: 12px; width: 100%; padding: 14px 16px; margin-bottom: 12px; background: linear-gradient(135deg, var(--mf-primary) 0%, var(--mf-accent) 100%); border: none; border-radius: 12px; color: #fff; cursor: pointer; box-shadow: var(--mf-shadow-sm); transition: transform 0.15s ease; }
.ai-entry-card:active { transform: scale(0.98); }
.ai-entry-icon { font-size: 28px; opacity: 0.9; }
.ai-entry-text { flex: 1; display: flex; flex-direction: column; gap: 2px; text-align: left; }
.ai-entry-text b { font-size: 15px; font-weight: 700; }
.ai-entry-text span { font-size: 11px; opacity: 0.8; }
.ai-entry-arrow { font-size: 18px; opacity: 0.7; }
.quick-actions-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 14px;
}
.quick-action-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 5px;
  padding: 10px 4px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  color: var(--mf-text-main);
  font-size: 11px;
  cursor: pointer;
}
.qa-icon { font-size: 20px; color: var(--mf-primary); }
.kpi-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}
.kpi-card {
  min-width: 0;
  overflow: hidden;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 12px 14px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  box-shadow: var(--mf-shadow-sm);
}
.kpi-card.hero-kpi {
  grid-column: 1 / -1;
  padding: 14px 16px;
}
.kpi-card.cyan b { color: var(--mf-info); } .kpi-card.red b { color: var(--mf-danger); } .kpi-card.green b { color: var(--mf-success); }
.kpi-card span { color: var(--mf-text-muted); font-size: 12px; }
.kpi-card b {
  font-size: clamp(15px, 4.2vw, 18px);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-family: 'JetBrains Mono', monospace;
}
.kpi-card.hero-kpi b {
  font-size: clamp(20px, 5.5vw, 24px);
}
.mini-row { display: flex; justify-content: space-between; gap: 8px; margin: 12px 0; }
.mini-row > div { display: flex; flex-direction: column; gap: 4px; min-width: 0; flex: 1; }
.mini-row span { color: var(--mf-text-muted); font-size: 12px; }
.mini-row b { font-size: clamp(13px, 3.6vw, 15px); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-family: 'JetBrains Mono', monospace; }
.chart-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 12px 14px;
  margin-top: 12px;
}
.chart-card-head {
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-muted);
  margin-bottom: 8px;
}
.trend-chart-wrap { height: 190px; }
.pie-chart-wrap { height: 220px; }
.section-title { margin: 16px 0 8px; font-size: 14px; color: var(--mf-text-muted); }
.record-card { display: flex; justify-content: space-between; align-items: center; gap: 8px; padding: 12px; border-bottom: 1px solid var(--mf-border); cursor: pointer; }
.record-card .cat { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.record-card span:last-child { flex-shrink: 0; white-space: nowrap; }
.income { color: var(--mf-income); } .expense { color: var(--mf-expense); }
.empty-state { text-align: center; color: var(--mf-text-muted); padding: 20px 0; font-size: 13px; }
</style>
