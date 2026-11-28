<template>
  <div class="expenses-mobile">
    <div class="page-header">
      <div class="mobile-segmented">
        <button class="segmented-item active">{{ t('nav.dailyExpenses') }}</button>
        <button class="segmented-item" @click="$router.push('/m/transactions')">{{ t('nav.transactions') }}</button>
      </div>
      <el-button type="primary" plain @click="openScanner">
        <Icon icon="ph:camera-bold" style="margin-right: 4px" />
        扫票记账
      </el-button>
    </div>

    <div class="summary-strip-card">
      <div class="strip-col">
        <span class="strip-lbl">收入</span>
        <span class="strip-val income mf-amount-mask">{{ formatCurrency(month?.total_income ?? 0) }}</span>
      </div>
      <div class="strip-col">
        <span class="strip-lbl">支出</span>
        <span class="strip-val expense mf-amount-mask">{{ formatCurrency(month?.total_expense ?? 0) }}</span>
      </div>
      <div class="strip-col">
        <span class="strip-lbl">结余</span>
        <span class="strip-val mf-amount-mask">{{ formatCurrency(month?.balance ?? ((month?.total_income ?? 0) - (month?.total_expense ?? 0))) }}</span>
      </div>
    </div>

    <!-- 紧凑筛选栏 -->
    <div class="filter-bar">
      <el-date-picker
        v-model="filters.month"
        type="month"
        value-format="YYYY-MM"
        placeholder="全部月份"
        clearable
        :editable="false"
        class="month-picker"
        @change="onFilterChange"
      />
      <div class="chip-scroll">
        <button
          v-for="opt in typeOptions"
          :key="opt.value"
          type="button"
          class="filter-chip"
          :class="{ active: filters.expense_type === opt.value }"
          @click="selectType(opt.value)"
        >
          {{ opt.label }}
        </button>
      </div>
      <el-button plain class="filter-trigger-btn" @click="openFilterDrawer">
        <Icon icon="ph:funnel-simple" style="margin-right: 4px" />
        筛选<span v-if="activeFilterCount > 0" class="filter-count">{{ activeFilterCount }}</span>
      </el-button>
    </div>

    <el-empty v-if="!loading && list.length === 0" description="暂无收支记录" :image-size="72">
      <el-button type="primary" @click="create">记一笔</el-button>
    </el-empty>
    <div v-for="e in list" :key="e.id" class="expense-card" @click="edit(e)">
      <div class="top">
        <span class="cat">{{ e.category_name || '未分类' }}</span>
        <span :class="[e.expense_type === 'income' ? 'income' : 'expense', 'mf-amount-mask']">
          {{ e.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(e.amount, e.currency) }}
        </span>
      </div>
      <div class="bottom">
        <span>{{ e.expense_date }}<template v-if="e.note"> · {{ e.note }}</template></span>
        <span class="asset-name">{{ e.asset_name }}</span>
      </div>
      <div v-if="normalizeTags(e.tags).length" class="card-tags">
        <el-tag v-for="tg in normalizeTags(e.tags)" :key="tg.id || tg.name" :color="tg.color" size="small" effect="dark" class="mobile-tag">
          {{ tg.name }}
        </el-tag>
      </div>
    </div>

    <div v-if="list.length > 0 && hasMore" class="load-more-wrap">
      <el-button plain class="load-more-btn" :loading="loading" @click="loadMore">
        加载更多
      </el-button>
    </div>

    <el-button class="fab" type="primary" circle :icon="Plus" @click="create" />
    <ExpenseQuickSheet v-model="sheetOpen" :record="editing" @saved="onSaved" />
    <ReceiptScannerModal v-model="scannerOpen" @created="onSaved" />

    <el-drawer
      v-model="filterDrawer"
      direction="btt"
      size="auto"
      title="筛选收支记录"
      class="mobile-filter-drawer"
    >
      <div class="drawer-form">
        <div class="drawer-field">
          <label>分类</label>
          <el-select v-model="filters.category_id" placeholder="全部分类" clearable style="width: 100%">
            <el-option v-for="c in filterCategories" :key="c.id" :label="c.label" :value="String(c.id)" />
          </el-select>
        </div>
        <div class="drawer-field">
          <label>标签</label>
          <el-select v-model="filters.tag_ids" multiple collapse-tags placeholder="全部标签" clearable style="width: 100%">
            <el-option v-for="tg in tagOptions" :key="tg.id" :label="tg.name" :value="tg.id" />
          </el-select>
        </div>
        <div class="drawer-field">
          <label>备注关键字</label>
          <el-input v-model="filters.keyword" placeholder="搜索备注内容…" clearable />
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
import { Plus } from '@element-plus/icons-vue'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { tagsApi } from '@/api/tags'
import { useLedgerStore } from '@/stores/ledger'
import { useCategoryStore } from '@/stores/category'
import { query, rowsFrom } from '@/db/local'
import { formatCurrency, localThisMonth } from '@/utils/format'
import { t } from '@/utils/locale'
import type { DailyExpense, ExpenseMonthly, Tag, Category } from '@/types'
import ExpenseQuickSheet from './ExpenseQuickSheet.vue'
import ReceiptScannerModal from '@/components/ReceiptScannerModal.vue'

const PAGE_SIZE = 20
const list = ref<DailyExpense[]>([])
const month = ref<ExpenseMonthly | null>(null)
const page = ref(1)
const loading = ref(false)
const hasMore = ref(true)
const sheetOpen = ref(false)
const scannerOpen = ref(false)
const filterDrawer = ref(false)
const editing = ref<DailyExpense | null>(null)
const ledgerStore = useLedgerStore()
const categoryStore = useCategoryStore()
const tagOptions = ref<Tag[]>([])
const localCategoryRows = ref<Category[]>([])

const filters = reactive({
  month: localThisMonth(),
  expense_type: '',
  category_id: '',
  tag_ids: [] as number[],
  keyword: '',
})

const typeOptions = [
  { label: '全部', value: '' },
  { label: '支出', value: 'expense' },
  { label: '收入', value: 'income' },
]

const activeFilterCount = computed(() => {
  let count = 0
  if (filters.category_id) count++
  if (filters.tag_ids.length > 0) count++
  if (filters.keyword.trim()) count++
  return count
})

const filterCategories = computed(() => {
  const result: { id: number; label: string }[] = []
  const seen = new Set<number>()
  function walk(nodes: Category[], prefix = '') {
    for (const c of nodes) {
      if (c.type !== 'expense' && c.type !== 'income') continue
      if (filters.expense_type && c.type !== filters.expense_type) continue
      const label = prefix ? `${prefix} / ${c.name}` : c.name
      if (!seen.has(c.id)) {
        seen.add(c.id)
        result.push({ id: c.id, label })
      }
      if (c.children?.length) walk(c.children, label)
    }
  }
  walk(categoryStore.allNodes?.length ? categoryStore.allNodes : localCategoryRows.value)
  return result
})

function monthRange(ym: string): { start?: string; end?: string; year: number; monthNum: number } {
  const now = new Date()
  if (!ym || !/^\d{4}-\d{2}$/.test(ym)) {
    return { year: now.getFullYear(), monthNum: now.getMonth() + 1 }
  }
  const year = Number(ym.slice(0, 4))
  const monthNum = Number(ym.slice(5, 7))
  const lastDay = new Date(year, monthNum, 0).getDate()
  return {
    year,
    monthNum,
    start: `${ym}-01`,
    end: `${ym}-${String(lastDay).padStart(2, '0')}`,
  }
}

function loadFilterOptions() {
  try {
    localCategoryRows.value = rowsFrom(
      query("SELECT * FROM categories WHERE __deleted = 0 AND type IN ('expense','income') ORDER BY sort_order, id"),
    ) as unknown as Category[]
    tagOptions.value = rowsFrom(query('SELECT * FROM tags WHERE __deleted = 0 ORDER BY id')) as unknown as Tag[]
  } catch {
    /* localDb unavailable */
  }
  if (!categoryStore.allNodes?.length && typeof categoryStore.loadCategories === 'function') {
    categoryStore.loadCategories().catch(() => {})
  }
  if (tagOptions.value.length === 0 && typeof tagsApi.list === 'function') {
    tagsApi.list({ page_size: 100 }).then((res) => {
      if (res?.list) tagOptions.value = res.list
    }).catch(() => {})
  }
}

/**
 * 离线读取：从本地 sql.js 镜像分页查询收支记录，并关联出分类/资产名称。
 * 仅在网络请求失败时调用，避免覆盖线上更新的数据。
 */
function loadLocalList(reset: boolean): void {
  const currentLedgerId = ledgerStore.currentLedgerId || 0
  const offset = (page.value - 1) * PAGE_SIZE
  const where: string[] = ['e.__deleted = 0', '(e.ledger_id = ? OR e.ledger_id = 0)']
  const params: any[] = [currentLedgerId]

  if (filters.expense_type) {
    where.push('e.expense_type = ?')
    params.push(filters.expense_type)
  }
  if (filters.category_id) {
    where.push('e.category_id = ?')
    params.push(Number(filters.category_id))
  }
  const { start, end } = monthRange(filters.month)
  if (start && end) {
    where.push('e.expense_date >= ? AND e.expense_date <= ?')
    params.push(start, end)
  }
  if (filters.keyword.trim()) {
    where.push('LOWER(COALESCE(e.note, \'\')) LIKE ?')
    params.push(`%${filters.keyword.trim().toLowerCase()}%`)
  }
  if (filters.tag_ids.length > 0) {
    const placeholders = filters.tag_ids.map(() => '?').join(',')
    where.push(`EXISTS (SELECT 1 FROM expense_tags et WHERE et.expense_id = e.id AND et.__deleted = 0 AND et.tag_id IN (${placeholders}))`)
    params.push(...filters.tag_ids)
  }

  params.push(PAGE_SIZE, offset)
  const res = query(
    `SELECT e.id, e.category_id, e.asset_id, e.expense_type, e.amount, e.currency,
            e.expense_date, e.note, e.created_at, e.updated_at,
            c.name AS category_name, a.name AS asset_name
       FROM daily_expenses e
       LEFT JOIN categories c ON c.id = e.category_id
       LEFT JOIN assets a ON a.id = e.asset_id
      WHERE ${where.join(' AND ')}
      ORDER BY e.expense_date DESC, e.id DESC
      LIMIT ? OFFSET ?`,
    params,
  )
  const rows = rowsFrom(res) as unknown as DailyExpense[]
  list.value = reset ? rows : [...list.value, ...rows]
  hasMore.value = rows.length >= PAGE_SIZE
}

/** 离线读取：从本地 sql.js 镜像聚合指定月份的收支合计 */
function loadLocalMonth(year: number, monthNum: number): void {
  const currentLedgerId = ledgerStore.currentLedgerId || 0
  const prefix = `${String(year).padStart(4, '0')}-${String(monthNum).padStart(2, '0')}`
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
  month.value = {
    total_income: totalIncome,
    total_expense: totalExpense,
    balance: totalIncome - totalExpense,
  } as unknown as ExpenseMonthly
}

async function loadData(reset = false) {
  if (reset) page.value = 1
  loading.value = true
  try {
    const { year, monthNum, start, end } = monthRange(filters.month)
    const listParams: Record<string, any> = {
      page: page.value,
      page_size: PAGE_SIZE,
    }
    if (filters.expense_type) listParams.expense_type = filters.expense_type
    if (filters.category_id) listParams.category_id = filters.category_id
    if (filters.tag_ids.length > 0) listParams.tag_ids = filters.tag_ids.join(',')
    if (start && end) {
      listParams.start_date = start
      listParams.end_date = end
    }

    const [res, m] = await Promise.allSettled([
      dailyExpensesApi.list(listParams),
      dailyExpensesApi.monthly(year, monthNum),
    ])
    if (res.status === 'fulfilled') {
      const kw = filters.keyword.trim().toLowerCase()
      const pageItems = kw
        ? res.value.list.filter((item) => (item.note || '').toLowerCase().includes(kw) || (item.category_name || '').toLowerCase().includes(kw))
        : res.value.list
      list.value = reset ? pageItems : [...list.value, ...pageItems]
      const total = Number(res.value.total || 0)
      const fetchedCount = reset ? res.value.list.length : list.value.length
      hasMore.value = total > 0 ? fetchedCount < total && res.value.list.length >= PAGE_SIZE : res.value.list.length >= PAGE_SIZE
    } else {
      try {
        loadLocalList(reset)
      } catch (e) {
        console.error('[DailyExpensesMobile] loadLocalList failed:', e)
      }
    }
    if (m.status === 'fulfilled') {
      month.value = m.value
    } else {
      try {
        loadLocalMonth(year, monthNum)
      } catch (e) {
        console.error('[DailyExpensesMobile] loadLocalMonth failed:', e)
      }
    }
  } catch (err) {
    console.error('[DailyExpensesMobile] loadData failed:', err)
    try {
      loadLocalList(reset)
    } catch (e) {
      console.error('[DailyExpensesMobile] loadLocalList failed:', e)
    }
  } finally {
    loading.value = false
  }
}

function normalizeTags(raw: any): any[] {
  if (!raw) return []
  let res = raw
  if (typeof raw === 'string') {
    try {
      res = JSON.parse(raw)
    } catch {
      return []
    }
  }
  if (!Array.isArray(res)) return []
  return res.filter((tg: any) => tg && typeof tg === 'object' && tg.name)
}

function selectType(val: string) {
  filters.expense_type = val
  loadData(true)
}

function onFilterChange() {
  loadData(true)
}

function openFilterDrawer() {
  loadFilterOptions()
  filterDrawer.value = true
}

function resetFilters() {
  filters.category_id = ''
  filters.tag_ids = []
  filters.keyword = ''
  filterDrawer.value = false
  loadData(true)
}

function applyFilters() {
  filterDrawer.value = false
  loadData(true)
}

function openScanner() {
  if (typeof navigator !== 'undefined' && navigator.onLine === false) {
    ElMessage.warning('扫票记账需要网络连接')
    return
  }
  scannerOpen.value = true
}

function loadMore() {
  if (loading.value || !hasMore.value) return
  page.value++
  loadData()
}
function create() { editing.value = null; sheetOpen.value = true }
function edit(e: DailyExpense) { editing.value = e; sheetOpen.value = true }
function onSaved() { sheetOpen.value = false; scannerOpen.value = false; loadData(true) }

onMounted(() => {
  loadFilterOptions()
  loadData(true)
})
</script>

<style scoped>
.expenses-mobile { padding-bottom: 72px; }
.page-header { display: flex; justify-content: space-between; align-items: center; gap: 8px; margin-bottom: 12px; }
.summary-strip-card {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 8px;
  margin: 12px 0;
  padding: 12px;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
}
.strip-col { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
.strip-lbl { font-size: 11px; color: var(--mf-text-muted); }
.strip-val { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 600; color: var(--mf-text-main); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.filter-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.month-picker { width: 124px !important; flex-shrink: 0; }
.chip-scroll {
  display: flex;
  align-items: center;
  gap: 6px;
  flex: 1;
  min-width: 0;
  overflow-x: auto;
}
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
.expense-card { background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 14px; margin-bottom: 10px; cursor: pointer; }
.expense-card .top { display: flex; justify-content: space-between; align-items: center; gap: 8px; font-size: 16px; }
.expense-card .top .cat { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.expense-card .bottom { display: flex; justify-content: space-between; align-items: center; gap: 8px; color: var(--mf-text-muted); font-size: 12px; margin-top: 6px; }
.expense-card .bottom > span:first-child { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.expense-card .bottom .asset-name { flex-shrink: 0; }
.income { color: var(--mf-income); flex-shrink: 0; white-space: nowrap; } .expense { color: var(--mf-expense); flex-shrink: 0; white-space: nowrap; }
.card-tags { display: flex; flex-wrap: wrap; gap: 4px; margin-top: 8px; }
.mobile-tag { border: none; font-size: 11px; padding: 0 6px; height: 20px; line-height: 20px; }
.load-more-wrap { display: flex; justify-content: center; margin: 16px 0 24px; }
.load-more-btn { width: 100%; border-radius: var(--mf-radius-md); color: var(--mf-text-muted); }
.fab { position: fixed; z-index: var(--mf-z-floating, 120); right: calc(20px + env(safe-area-inset-right)); bottom: calc(76px + env(safe-area-inset-bottom)); width: 56px; height: 56px; box-shadow: var(--mf-shadow-glow); }
.drawer-form { display: flex; flex-direction: column; gap: 12px; padding-bottom: 12px; }
.drawer-field { display: flex; flex-direction: column; gap: 6px; }
.drawer-field label { font-size: 12px; color: var(--mf-text-muted); }
.drawer-actions { display: flex; gap: 10px; margin-top: 6px; }
.drawer-actions .el-button { flex: 1; }
</style>
