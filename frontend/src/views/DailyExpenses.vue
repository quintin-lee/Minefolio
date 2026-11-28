<template>
  <div class="daily-expenses-page">
    <PageHeader :title="t('nav.dailyExpenses')">
      <template #actions>
        <el-button type="primary" class="action-btn" @click="openDialog()">
          <el-icon><Plus /></el-icon> 新增记录
        </el-button>
        <el-button class="action-btn" @click="receiptScanVisible = true">
          <el-icon><Camera /></el-icon> 拍照/票据记账
        </el-button>
        <el-button class="action-btn" @click="exportCsv">
          <el-icon><Download /></el-icon> 导出 CSV
        </el-button>
        <el-button class="action-btn" @click="importDialogVisible = true">
          <el-icon><Upload /></el-icon> 导入 CSV
        </el-button>
      </template>
    </PageHeader>

    <el-row :gutter="16" class="daily-content-row">
      <!-- 左侧：记账列表 -->
      <el-col :xs="24" :lg="16" class="daily-main-col">
        <div class="main-panel">
          <div class="filter-panel">
            <el-form :inline="true" class="premium-filters">
              <el-form-item label="类型">
                <el-select v-model="filters.type" placeholder="全部" clearable class="filter-select">
                  <el-option label="收入" value="income" />
                  <el-option label="支出" value="expense" />
                </el-select>
              </el-form-item>
              <el-form-item label="月份">
                <el-date-picker v-model="filters.month" type="month" placeholder="选择月份" value-format="YYYY-MM" class="filter-date" />
              </el-form-item>
              <el-form-item class="filter-actions">
                <el-button type="primary" class="search-btn" @click="handleSearch">查询</el-button>
              </el-form-item>
            </el-form>
          </div>

          <!-- 月度汇总 -->
          <el-row :gutter="16" class="monthly-summary">
            <el-col :xs="24" :sm="8">
              <SummaryCard label="本月收入" :value="formatCurrency(monthSummary?.total_income ?? 0)" :type="(monthSummary?.total_income ?? 0) > 0 ? 'income' : 'neutral'" />
            </el-col>
            <el-col :xs="24" :sm="8">
              <SummaryCard label="本月支出" :value="formatCurrency(monthSummary?.total_expense ?? 0)" :type="(monthSummary?.total_expense ?? 0) > 0 ? 'expense' : 'neutral'" />
            </el-col>
            <el-col :xs="24" :sm="8">
              <SummaryCard label="本月结余" :value="formatCurrency(monthSummary?.balance ?? 0)" :type="(monthSummary?.balance ?? 0) > 0 ? 'income' : (monthSummary?.balance ?? 0) < 0 ? 'expense' : 'neutral'" />
            </el-col>
          </el-row>

          <div class="table-container">
            <el-table ref="tableRef" v-loading="loading" :data="expenses" class="premium-table stream-table" row-class-name="premium-row" header-cell-class-name="premium-header" @selection-change="handleSelectionChange">
              <el-table-column type="selection" width="42" align="center" />
              <el-table-column prop="expense_date" label="日期" width="98" />
              <el-table-column v-if="!isNarrowTable" prop="asset_name" label="关联资产" min-width="104" show-overflow-tooltip />
              <el-table-column prop="category_name" label="分类" min-width="112" show-overflow-tooltip>
                <template #default="{ row }">
                  <div class="category-cell-stack">
                    <span class="category-main-name">{{ row.category_name }}</span>
                    <span v-if="isCompactTable && (row.note || (isNarrowTable && row.asset_name))" class="category-sub-hint">
                      {{ [isNarrowTable ? row.asset_name : '', row.note].filter(Boolean).join(' · ') }}
                    </span>
                  </div>
                </template>
              </el-table-column>
              <el-table-column prop="expense_type" label="类型" width="84">
                <template #default="{ row }">
                  <el-tag :type="row.expense_type === 'income' ? 'success' : 'danger'" effect="light" class="type-badge" round>
                    {{ row.expense_type === 'income' ? '收入' : '支出' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column prop="amount" label="金额" min-width="112" align="right">
                <template #default="{ row }">
                  <span :class="['mono-amount', row.expense_type === 'income' ? 'income-text' : 'expense-text']">
                    {{ row.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(row.amount) }}
                  </span>
                </template>
              </el-table-column>
              <el-table-column v-if="!isCompactTable" prop="note" label="备注" min-width="110" show-overflow-tooltip />
              <el-table-column v-if="!isCompactTable" label="标签" min-width="96">
                <template #default="{ row }">
                  <div class="tag-list">
                    <el-tag v-for="tag in normalizeTags((row as any).tags)" :key="tag.id || tag.name" :color="tag.color" size="small" effect="dark" class="custom-tag">
                      {{ tag.name }}
                    </el-tag>
                  </div>
                </template>
              </el-table-column>
              <el-table-column label="操作" width="104" align="center" fixed="right">
                <template #default="{ row }">
                  <div class="action-buttons">
                    <el-button link type="primary" @click="openDialog(row as any)">编辑</el-button>
                    <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
                  </div>
                </template>
              </el-table-column>
              <template #empty>
                <el-empty description="暂无收支记录">
                  <el-button type="primary" :icon="Plus" @click="openDialog()">记一笔</el-button>
                </el-empty>
              </template>
            </el-table>
            <div v-if="total > 0" class="pagination-bar">
              <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadData" @size-change="handleSizeChange" />
            </div>
          </div>
        </div>
      </el-col>

      <!-- 右侧：月度图表 -->
      <el-col :xs="24" :lg="8" class="daily-charts-col">
        <div class="chart-panel">
          <h3 class="panel-title">月度收支趋势</h3>
          <MonthlyChart :data="monthSummary" />
        </div>
        <div class="chart-panel">
          <h3 class="panel-title">分类占比</h3>
          <ExpenseCategoryPie :data="monthSummary?.by_category ?? []" />
        </div>
      </el-col>
    </el-row>

    <!-- 批量操作悬浮条 -->
    <transition name="slide-up">
      <div v-if="selectedExpenses.length > 0" class="floating-batch-bar">
        <div class="batch-bar-left">
          <span class="selected-badge">{{ selectedExpenses.length }}</span>
          <span>项已选中</span>
        </div>
        <div class="batch-bar-actions">
          <el-button size="small" type="danger" :loading="isBatchDeleting" @click="handleBatchDelete">
            <el-icon><Delete /></el-icon> 批量删除
          </el-button>
          <el-button size="small" @click="handleBatchExport">
            <el-icon><Download /></el-icon> 导出所选
          </el-button>
          <el-button size="small" link @click="clearSelection">取消选择</el-button>
        </div>
      </div>
    </transition>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑收支' : '新增收支'" width="500px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-form-item label="类型" prop="expense_type">
          <el-radio-group v-model="form.expense_type" class="type-radio-group">
            <el-radio-button value="income">收入</el-radio-button>
            <el-radio-button value="expense">支出</el-radio-button>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="分类" prop="category_id">
          <el-cascader
            :key="form.expense_type"
            v-model="form._catPath"
            :options="categoryTree as any"
            :props="{ checkStrictly: true, value: 'id', label: 'name', children: 'children' }"
            placeholder="选择分类"
            style="width: 100%"
            clearable
            @change="onCatChange"
          />
        </el-form-item>
        <el-form-item label="关联资产" prop="asset_id">
          <el-select v-model="form.asset_id" placeholder="选择资产" style="width: 100%" filterable>
            <el-option v-for="a in allAssets" :key="a.id" :label="`${assetLabel(a)}（${maskAmountText(`${a.currency} ${Number(a.current_value).toFixed(2)}`)}）`" :value="Number(a.id)">
              <span>{{ assetLabel(a) }}</span>
              <el-tag v-if="a.asset_type === 'loan' || a.asset_type === 'credit_card' || a.asset_type === 'other_liability'" size="small" type="warning" effect="light" style="margin-left: 8px">负债</el-tag>
              <span class="mf-amount-mask" style="float: right; color: var(--mf-text-muted); font-size: 13px">{{ a.currency }} {{ Number(a.current_value).toFixed(2) }}</span>
            </el-option>
          </el-select>
        </el-form-item>
        <el-form-item label="金额" prop="amount">
          <el-input-number v-model="form.amount" :precision="2" :min="0.01" style="width: 100%" :controls="false" class="hero-amount-input" />
        </el-form-item>
        <el-form-item label="日期" prop="expense_date">
          <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
        </el-form-item>
        <el-form-item label="标签">
          <TagPicker v-model="form.tags" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" type="textarea" :rows="3" placeholder="添加备注..." />
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" class="save-btn" :loading="saving" @click="handleSubmit">保存记录</el-button>
        </div>
      </template>
    </el-dialog>

    <!-- 导入 CSV 对话框 -->
    <el-dialog v-model="importDialogVisible" title="导入收支记录 (CSV)" width="520px" class="premium-dialog" :show-close="false">
      <div class="import-dialog-content">
        <p class="import-hint">
          CSV 格式：日期, 资产名称, 分类名称, 类型(expense/income), 金额, 币种(可选), 备注(可选)<br>
          <span class="muted-text">示例：2024-01-15, 现金账户, 餐饮美食, expense, 50, CNY, 午餐</span>
        </p>
        <el-upload ref="uploadRef" action="" :auto-upload="false" :limit="1" accept=".csv" drag class="import-upload" @change="handleFileChange">
          <el-icon class="el-icon--upload"><UploadFilled /></el-icon>
          <div class="el-upload__text">拖拽文件到此处，或 <em>点击选择</em></div>
          <template #tip><div class="el-upload__tip">仅支持 .csv 文件，UTF-8 编码</div></template>
        </el-upload>
        <div v-if="importTask.running.value" class="import-progress-box">
          <el-progress :percentage="importTask.progress.value" :stroke-width="6" />
          <div class="muted-text" style="margin-top: 6px; font-size: 12px">{{ importTask.message.value || '正在导入日常收支...' }}</div>
        </div>
        <div v-if="importResult" class="import-result" :class="importResult.ok ? 'success' : 'error'">
          <el-icon><SuccessFilled /></el-icon>
          <span>成功导入 {{ importResult.imported }} 条，失败 {{ importResult.errors }} 条</span>
          <span v-if="importResult.errors_detail" class="import-errors">{{ importResult.errors_detail }}</span>
        </div>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="importTask.cancel(); importDialogVisible = false; importResult = null; importFile = null; importText = null; uploadRef?.clearFiles()">关闭</el-button>
          <el-button type="primary" class="save-btn" :loading="importing" @click="handleImport" :disabled="!importText">开始导入</el-button>
        </div>
      </template>
    </el-dialog>

    <!-- AI 票据拍照识别记账对话框 -->
    <ReceiptScannerModal
      v-model="receiptScanVisible"
      @created="loadData"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, onBeforeUnmount, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, Download, Upload, UploadFilled, SuccessFilled, Camera, Delete } from '@element-plus/icons-vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { assetsApi } from '@/api/assets'
import { useCategoryStore } from '@/stores/category'
import { t } from '@/utils/locale'
import PageHeader from '@/components/PageHeader.vue'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import ReceiptScannerModal from '@/components/ReceiptScannerModal.vue'
import { formatCurrency, localToday, localThisMonth, assetLabel } from '@/utils/format'
import { maskAmountText } from '@/stores/display-settings'
import { findCategoryPath, prepareCategoryTree, isIdInTree } from '@/utils/category-tree'
import { useAsyncTaskProgress } from '@/composables/useAsyncTaskProgress'
import { exportChartCsv } from '@/utils/chart-export'
import SummaryCard from '@/components/SummaryCard.vue'
import type { DailyExpense, Tag, Asset } from '@/types'

const expenses = ref<DailyExpense[]>([])
const allAssets = ref<Asset[]>([])
const categoryStore = useCategoryStore()
const allCategories = computed(() => categoryStore.incomeExpenseCategories)
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const receiptScanVisible = ref(false)

const tableRef = ref()
const selectedExpenses = ref<DailyExpense[]>([])
const isBatchDeleting = ref(false)

const COMPACT_TABLE_QUERY = '(max-width: 1500px)'
const NARROW_TABLE_QUERY = '(max-width: 900px)'
const isCompactTable = ref(
  typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    ? window.matchMedia(COMPACT_TABLE_QUERY).matches
    : false,
)
const isNarrowTable = ref(
  typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    ? window.matchMedia(NARROW_TABLE_QUERY).matches
    : false,
)
let compactMediaQuery: MediaQueryList | null = null
let narrowMediaQuery: MediaQueryList | null = null
const handleCompactTableChange = (e: MediaQueryListEvent) => {
  isCompactTable.value = e.matches
}
const handleNarrowTableChange = (e: MediaQueryListEvent) => {
  isNarrowTable.value = e.matches
}

const form = reactive({ expense_type: 'expense' as 'income' | 'expense', category_id: null as number | null, asset_id: null as number | null, amount: 0, expense_date: '', note: '', tags: [] as Tag[], _catPath: [] as number[] })
const rules = {
  expense_type: [{ required: true }],
  category_id: [{ required: true }],
  asset_id: [{ required: true, message: '请选择关联资产', trigger: 'change' }],
  amount: [{ required: true, type: 'number' as const, min: 0.01, message: '金额必须大于 0', trigger: 'change' }],
  expense_date: [{ required: true }],
}

const categoryTree = computed(() => {
  return prepareCategoryTree(allCategories.value, form.expense_type)
})

watch(() => form.expense_type, () => {
  // Clear category selection if selected category is not of the new type
  if (form.category_id && !isIdInTree(categoryTree.value, form.category_id)) {
    form.category_id = null
    form._catPath = []
  }
})

const monthSummary = ref<any>(null)
const filters = reactive({ type: '', month: '' })
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const loading = ref(false)
const saving = ref(false)
const formRef = ref()

const uploadRef = ref()
let loadReqSeq = 0

async function loadData() {
  const reqId = ++loadReqSeq
  loading.value = true
  try {
    const params: any = { page: page.value, page_size: pageSize.value }
    if (filters.type) params.expense_type = filters.type
    const now = new Date()
    let y = now.getFullYear()
    let m = now.getMonth() + 1
    if (filters.month) {
      const [pYear, pMonth] = filters.month.split('-')
      if (pYear && pMonth) {
        y = parseInt(pYear)
        m = parseInt(pMonth)
        params.start_date = `${pYear}-${pMonth}-01`
        const lastDay = new Date(y, m, 0).getDate()
        params.end_date = `${pYear}-${pMonth}-${String(lastDay).padStart(2, '0')}`
      }
    }
    const [res, mr] = await Promise.allSettled([
      dailyExpensesApi.list(params),
      dailyExpensesApi.monthly(y, m)
    ])
    if (reqId !== loadReqSeq) return
    if (res.status === 'fulfilled') {
      expenses.value = res.value.list
      total.value = res.value.total
    }
    if (mr.status === 'fulfilled') {
      monthSummary.value = mr.value
    }
  } catch (err) {
    console.error('[DailyExpenses] loadData failed:', err)
  } finally {
    if (reqId === loadReqSeq) loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  loadData()
}

function handleSizeChange() {
  page.value = 1
  loadData()
}

function normalizeTags(raw: any): Tag[] {
  if (!raw) return []
  let list = raw
  if (typeof raw === 'string') {
    try {
      list = JSON.parse(raw)
    } catch {
      return []
    }
  }
  if (!Array.isArray(list)) return []
  return list.filter((t: any) => t && typeof t === 'object' && t.name)
}

function onCatChange(val: any) { const last = (val as any[])?.[(val as any[]).length - 1]; form.category_id = last != null ? Number(last) : null }

function openDialog(expense?: any) {
  editingId.value = expense?.id ?? null
  const expenseType = expense?.expense_type || 'expense'
  const scopedCategories = allCategories.value.filter(c => c.type === expenseType)
  const catId = expense?.category_id ? Number(expense.category_id) : null
  const catPath = catId ? findCategoryPath(scopedCategories, catId) : []
  Object.assign(form, expense ? { expense_type: expense.expense_type, category_id: catId, asset_id: Number(expense.asset_id), amount: Number(expense.amount), expense_date: expense.expense_date, note: expense.note, tags: normalizeTags(expense.tags), _catPath: catPath.length ? catPath : (catId ? [catId] : []) }
    : { expense_type: 'expense', category_id: null, asset_id: null, amount: 0, expense_date: localToday(), note: '', tags: [], _catPath: [] })
  dialogVisible.value = true
}

async function handleSubmit() {
  if (saving.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid || saving.value) return
    saving.value = true
    try {
      const data = {
        expense_type: form.expense_type,
        category_id: form.category_id,
        asset_id: form.asset_id,
        amount: form.amount,
        expense_date: form.expense_date,
        note: form.note,
        tags: form.tags.map(t => ({
          id: t.id > 0 ? t.id : undefined,
          name: t.name,
          color: t.color,
        })),
      }
      if (editingId.value) await dailyExpensesApi.update(editingId.value, data)
      else await dailyExpensesApi.create(data)
      ElMessage.success('保存成功')
      dialogVisible.value = false
      loadData()
    } finally { saving.value = false }
  })
}

async function handleDelete(expense: any) {
  try {
    await ElMessageBox.confirm('确定删除该记录吗？', '提示', { type: 'warning' })
  } catch {
    return
  }
  try {
    await dailyExpensesApi.delete(expense.id)
    ElMessage.success('删除成功')
    if (expenses.value.length === 1 && page.value > 1) {
      page.value -= 1
    }
    loadData()
  } catch (err: any) {
    ElMessage.error(err?.message || '删除失败')
  }
}

function handleSelectionChange(rows: DailyExpense[]) {
  selectedExpenses.value = rows
}

function clearSelection() {
  tableRef.value?.clearSelection()
}

async function handleBatchDelete() {
  if (selectedExpenses.value.length === 0) return
  try {
    await ElMessageBox.confirm(
      `确定要删除选中的 ${selectedExpenses.value.length} 条收支记录吗？此操作无法撤销。`,
      '批量删除确认',
      {
        confirmButtonText: '确定删除',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )
    isBatchDeleting.value = true
    let successCount = 0
    let failCount = 0
    for (const item of selectedExpenses.value) {
      try {
        await dailyExpensesApi.delete(item.id)
        successCount++
      } catch {
        failCount++
      }
    }
    if (failCount === 0) {
      ElMessage.success(`成功删除 ${successCount} 条记录`)
    } else {
      ElMessage.warning(`删除完成：成功 ${successCount} 条，失败 ${failCount} 条`)
    }
    clearSelection()
    loadData()
  } catch (err: any) {
    if (err !== 'cancel') {
      ElMessage.error(err?.message || '批量删除失败')
    }
  } finally {
    isBatchDeleting.value = false
  }
}

function handleBatchExport() {
  if (selectedExpenses.value.length === 0) return
  const headers = ['日期', '关联资产', '分类', '类型', '金额', '备注', '标签']
  const rows = selectedExpenses.value.map(item => [
    item.expense_date,
    item.asset_name || '',
    item.category_name || '',
    item.expense_type === 'income' ? '收入' : '支出',
    item.amount,
    item.note || '',
    normalizeTags((item as any).tags).map((t: any) => t.name).join('; ') || '',
  ])
  const now = localToday()
  exportChartCsv(`daily_expenses_selected_${now}.csv`, headers, rows)
  ElMessage.success(`已导出 ${selectedExpenses.value.length} 条记录`)
}

onMounted(async () => {
  if (typeof window !== 'undefined' && typeof window.matchMedia === 'function') {
    compactMediaQuery = window.matchMedia(COMPACT_TABLE_QUERY)
    isCompactTable.value = compactMediaQuery.matches
    compactMediaQuery.addEventListener?.('change', handleCompactTableChange)

    narrowMediaQuery = window.matchMedia(NARROW_TABLE_QUERY)
    isNarrowTable.value = narrowMediaQuery.matches
    narrowMediaQuery.addEventListener?.('change', handleNarrowTableChange)
  }
  try {
    const [, assetRes] = await Promise.allSettled([
      categoryStore.loadCategories(),
      assetsApi.list({ page_size: 500 }),
    ])
    if (assetRes.status === 'fulfilled') {
      allAssets.value = assetRes.value.list
    }
    filters.month = localThisMonth()
    await loadData()
  } catch (err) {
    console.error('[DailyExpenses] onMounted failed:', err)
  }
})

// ── Import / Export ───────────────────────────────────────────────────────────
const importDialogVisible = ref(false)
const importFile = ref<File | null>(null)
const importText = ref<string | null>(null)
const importResult = ref<{ imported: number; errors: number; errors_detail?: string; ok: boolean } | null>(null)
const importing = ref(false)

function exportCsv() {
  dailyExpensesApi.exportCsv().then((blob) => {
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    const now = localToday()
    a.download = `daily_expenses_${now}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
  }).catch(() => ElMessage.error('导出失败'))
}

let activeImportReader: FileReader | null = null

onBeforeUnmount(() => {
  activeImportReader?.abort()
  activeImportReader = null
  compactMediaQuery?.removeEventListener?.('change', handleCompactTableChange)
  compactMediaQuery = null
  narrowMediaQuery?.removeEventListener?.('change', handleNarrowTableChange)
  narrowMediaQuery = null
})

function handleFileChange(raw: any) {
  activeImportReader?.abort()
  activeImportReader = null
  const file = raw.raw ?? null
  importFile.value = file
  importText.value = null
  importResult.value = null
  if (file) {
    const reader = new FileReader()
    activeImportReader = reader
    reader.onload = (e) => {
      if (activeImportReader !== reader) return
      activeImportReader = null
      importText.value = e.target?.result as string
    }
    reader.onerror = () => {
      if (activeImportReader !== reader) return
      activeImportReader = null
      ElMessage.error('读取 CSV 文件失败')
    }
    reader.readAsText(file, 'UTF-8')
  }
}

const importTask = useAsyncTaskProgress()

async function handleImport() {
  if (!importText.value) return
  importing.value = true
  importResult.value = null
  try {
    const text = importText.value
    const res = await importTask.run(
      (opts) => dailyExpensesApi.importCsv(text, opts),
      '正在上传并解析收支 CSV...',
    )
    importResult.value = { imported: res.imported, errors: res.errors, errors_detail: res.errors_detail, ok: true }
    if (res.errors === 0) {
      ElMessage.success(`成功导入 ${res.imported} 条记录`)
      importDialogVisible.value = false
      importFile.value = null
      importText.value = null
      importResult.value = null
      uploadRef.value?.clearFiles()
      loadData()
    }
  } catch (err: any) {
    if (importTask.isCancelledError(err)) return
    importResult.value = { imported: 0, errors: 1, errors_detail: err?.message || '导入失败', ok: false }
  } finally {
    importing.value = false
  }
}
</script>

<style scoped>
.daily-expenses-page {
  background-color: var(--mf-background);
  min-height: 100%;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.action-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
  padding: 8px 16px;
  transition: var(--mf-transition);
}

.action-btn:hover {
  transform: translateY(-1px);
}

.filter-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  margin-bottom: 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.premium-filters {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
}

.premium-filters :deep(.el-form-item) {
  margin-bottom: 0;
  margin-right: 0;
}

.premium-filters :deep(.el-form-item__label) {
  font-weight: 500;
  color: var(--mf-text-regular);
}

.filter-select {
  width: 170px;
}

.filter-date {
  width: 260px;
}

.filter-actions {
  margin-left: auto;
}

.search-btn {
  border-radius: var(--mf-radius-md);
}

.monthly-summary {
  margin-bottom: 24px;
  row-gap: 12px;
}

.summary-value {
  font-size: 24px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}

.main-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  display: flex;
  flex-direction: column;
}

.table-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 12px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  overflow-x: auto;
  display: flex;
  flex-direction: column;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-primary-light);
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

:deep(.premium-header th) {
  background-color: var(--mf-primary-light) !important;
  color: var(--mf-text-muted) !important;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border) !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border);
  padding: 16px 0;
  color: var(--mf-text-main);
}

:deep(.premium-row:hover > td) {
  background-color: var(--mf-primary-light) !important;
}

.type-badge {
  font-weight: 600;
  padding: 4px 12px;
  border-radius: 12px;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 15px;
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
  word-break: keep-all;
}

.table-container :deep(.el-table__fixed-right),
.table-container :deep(.el-table__fixed-right-patch) {
  background: var(--mf-surface-card) !important;
  box-shadow: -4px 0 12px rgba(0, 0, 0, 0.15);
}

.table-container :deep(.el-table__fixed-right .el-table__cell) {
  background: var(--mf-surface-card) !important;
}

.tag-list {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.custom-tag {
  border: none;
  border-radius: 6px;
  padding: 2px 8px;
  font-weight: 500;
  box-shadow: var(--mf-shadow-sm);
}

.action-buttons {
  display: flex;
  gap: 10px;
  justify-content: center;
  white-space: nowrap;
}

.category-cell-stack {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.category-main-name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.category-sub-hint {
  font-size: 11px;
  color: var(--mf-text-muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.daily-charts-col {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.chart-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.panel-title {
  margin: 0 0 20px 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

:deep(.premium-dialog) {
  border-radius: var(--mf-radius-xl);
  overflow: hidden;
  box-shadow: var(--mf-shadow-lg);
}

:deep(.premium-dialog .el-dialog__header) {
  margin: 0;
  padding: 24px;
  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
}

:deep(.premium-dialog .el-dialog__title) {
  font-weight: 600;
  font-size: 18px;
  color: var(--mf-text-main);
}

:deep(.premium-dialog .el-dialog__body) {
  padding: 32px 24px;
  background: var(--mf-background);
}

:deep(.premium-dialog .el-dialog__footer) {
  padding: 16px 24px;
  border-top: 1px solid var(--mf-border);
  background: var(--mf-surface);
  margin: 0;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.type-radio-group :deep(.el-radio-button__inner) {
  border-radius: var(--mf-radius-md) !important;
  margin-right: 12px;
  border: 1px solid var(--mf-border);
  box-shadow: none !important;
  padding: 10px 24px;
  font-weight: 500;
}

.type-radio-group :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: linear-gradient(135deg, var(--mf-primary), var(--mf-accent)) !important;
  border-color: var(--mf-primary) !important;
  color: #ffffff !important;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: var(--mf-surface-muted) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}

.cancel-btn, .save-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
}

.save-btn {
  padding: 8px 24px;
}

/* Natural vertical page flow inside Layout .main */

.floating-batch-bar {
  position: fixed;
  bottom: 32px;
  left: calc(50% + var(--mf-sidebar-width, 0px) / 2);
  transform: translateX(-50%);
  z-index: var(--mf-z-floating, 120);
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  align-items: center;
  gap: 10px 20px;
  max-width: calc(100vw - 24px);
  box-sizing: border-box;
  padding: 10px 20px;
  background: var(--mf-surface-card);
  border: 1px solid var(--mf-border);
  border-radius: 40px;
  box-shadow: var(--mf-shadow-lg);
  backdrop-filter: blur(12px);
}

.batch-bar-left {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  font-weight: 500;
  color: var(--mf-text-main);
}

.selected-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 22px;
  height: 22px;
  padding: 0 6px;
  border-radius: 11px;
  background-color: var(--mf-primary, #3b82f6);
  color: #ffffff;
  font-size: 12px;
  font-weight: 600;
}

.batch-bar-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
}

.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translate(-50%, 20px);
}

@media (max-width: 1320px) {
  .daily-content-row > .daily-main-col,
  .daily-content-row > .daily-charts-col {
    flex: 0 0 100%;
    max-width: 100%;
  }
  .daily-content-row > .daily-main-col {
    margin-bottom: 16px;
  }
  .daily-charts-col {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 16px;
  }
}

@media (max-width: 768px) {
  .daily-charts-col {
    grid-template-columns: 1fr;
  }
  .filter-panel {
    position: sticky;
    top: 0;
    z-index: var(--mf-z-header, 20);
    background: var(--mf-surface);
    backdrop-filter: blur(12px);
  }
  .table-container :deep(.el-table__body-wrapper) {
    overflow-x: auto !important;
  }
  .floating-batch-bar {
    bottom: calc(16px + env(safe-area-inset-bottom));
    padding: 10px 14px;
    border-radius: var(--mf-radius-lg);
    gap: 8px 12px;
  }
}

@media (max-width: 640px) {
  .table-container :deep(.el-table__fixed-right),
  .table-container :deep(.el-table__fixed-right-patch),
  :deep(.el-table-fixed-column--right) {
    position: static !important;
  }
}
</style>
