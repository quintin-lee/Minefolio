<template>
  <div class="daily-expenses-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>收支</h2>
      </div>
      <div class="header-actions">
        <el-button type="primary" class="action-btn" @click="openDialog()">
          <el-icon><Plus /></el-icon> 新增记录
        </el-button>
        <el-button class="action-btn" style="background:var(--mf-surface);color:var(--mf-text-main);border-color:var(--mf-border)" @click="exportCsv">
          <el-icon><Download /></el-icon> 导出 CSV
        </el-button>
        <el-button class="action-btn" style="background:var(--mf-surface);color:var(--mf-text-main);border-color:var(--mf-border)" @click="importDialogVisible = true">
          <el-icon><Upload /></el-icon> 导入 CSV
        </el-button>
      </div>
    </div>

    <el-row :gutter="24">
      <!-- 左侧：记账列表 -->
      <el-col :span="16">
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
            <el-col :span="8">
              <SummaryCard label="本月收入" :value="formatCurrency(monthSummary?.total_income ?? 0)" type="income" />
            </el-col>
            <el-col :span="8">
              <SummaryCard label="本月支出" :value="formatCurrency(monthSummary?.total_expense ?? 0)" type="expense" />
            </el-col>
            <el-col :span="8">
              <SummaryCard label="本月结余" :value="formatCurrency(monthSummary?.balance ?? 0)" type="highlight" />
            </el-col>
          </el-row>

          <div class="table-container">
            <el-table :data="expenses" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
              <el-table-column prop="expense_date" label="日期" width="110" />
              <el-table-column prop="asset_name" label="关联资产" min-width="110" />
              <el-table-column prop="category_name" label="分类" min-width="120" />
              <el-table-column prop="expense_type" label="类型" width="80">
                <template #default="{ row }">
                  <el-tag :type="row.expense_type === 'income' ? 'success' : 'danger'" effect="light" class="type-badge" round>
                    {{ row.expense_type === 'income' ? '收入' : '支出' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column prop="amount" label="金额" width="120" align="right">
                <template #default="{ row }">
                  <span :class="['mono-amount', row.expense_type === 'income' ? 'income-text' : 'expense-text']">
                    {{ row.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(row.amount) }}
                  </span>
                </template>
              </el-table-column>
              <el-table-column prop="note" label="备注" min-width="150" />
              <el-table-column label="标签" min-width="140">
                <template #default="{ row }">
                  <div class="tag-list">
                    <el-tag v-for="tag in (row as any).tags" :key="tag.id" :color="tag.color" size="small" effect="dark" class="custom-tag">
                      {{ tag.name }}
                    </el-tag>
                  </div>
                </template>
              </el-table-column>
              <el-table-column label="操作" width="120" align="center">
                <template #default="{ row }">
                  <div class="action-buttons">
                    <el-button link type="primary" @click="openDialog(row as any)">编辑</el-button>
                    <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
                  </div>
                </template>
              </el-table-column>
            </el-table>
            <div class="pagination-bar">
              <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadData" @size-change="handleSizeChange" />
            </div>
          </div>
        </div>
      </el-col>

      <!-- 右侧：月度图表 -->
      <el-col :span="8">
        <div class="chart-panel">
          <h3 class="panel-title">月度收支趋势</h3>
          <MonthlyChart :data="monthSummary" />
        </div>
        <div class="chart-panel" style="margin-top: 24px">
          <h3 class="panel-title">分类占比</h3>
          <ExpenseCategoryPie :data="monthSummary?.by_category ?? []" />
        </div>
      </el-col>
    </el-row>

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
          <el-cascader v-model="form._catPath" :options="categoryTree" :props="{ checkStrictly: true, value: 'id', label: 'name' }" placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>
        <el-form-item label="关联资产" prop="asset_id">
          <el-select v-model="form.asset_id" placeholder="选择资产" style="width: 100%" filterable>
            <el-option v-for="a in allAssets" :key="a.id" :label="`${a.name}（${a.currency} ${Number(a.current_value).toFixed(2)}）`" :value="Number(a.id)">
              <span>{{ a.name }}</span>
              <el-tag v-if="a.asset_type === 'loan' || a.asset_type === 'credit_card' || a.asset_type === 'other_liability'" size="small" type="warning" effect="light" style="margin-left: 8px">负债</el-tag>
              <span style="float: right; color: #475569; font-size: 13px">{{ a.currency }} {{ Number(a.current_value).toFixed(2) }}</span>
            </el-option>
          </el-select>
        </el-form-item>
        <el-form-item label="金额" prop="amount">
          <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" :controls="false" />
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
        <el-upload action="" :auto-upload="false" :limit="1" accept=".csv" drag class="import-upload" @change="handleFileChange">
          <el-icon class="el-icon--upload"><UploadFilled /></el-icon>
          <div class="el-upload__text">拖拽文件到此处，或 <em>点击选择</em></div>
          <template #tip><div class="el-upload__tip">仅支持 .csv 文件，UTF-8 编码</div></template>
        </el-upload>
        <div v-if="importResult" class="import-result" :class="importResult.ok ? 'success' : 'error'">
          <el-icon><SuccessFilled /></el-icon>
          <span>成功导入 {{ importResult.imported }} 条，失败 {{ importResult.errors }} 条</span>
          <span v-if="importResult.errors_detail" class="import-errors">{{ importResult.errors_detail }}</span>
        </div>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="importDialogVisible = false; importResult = null; importFile = null; importText = null">关闭</el-button>
          <el-button type="primary" class="save-btn" :loading="importing" @click="handleImport" :disabled="!importText">开始导入</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, Download, Upload, UploadFilled, SuccessFilled } from '@element-plus/icons-vue'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { assetsApi } from '@/api/assets'
import { useCategoryStore } from '@/stores/category'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import http from '@/utils/http'
import { formatCurrency } from '@/utils/format'
import SummaryCard from '@/components/SummaryCard.vue'
import type { DailyExpense, Tag, Category, Asset } from '@/types'

const expenses = ref<DailyExpense[]>([])
const allCategories = ref<Category[]>([])
const allAssets = ref<Asset[]>([])
const categoryStore = useCategoryStore()
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)

const form = reactive({ expense_type: 'expense' as 'income' | 'expense', category_id: null as number | null, asset_id: null as number | null, amount: 0, expense_date: '', note: '', tags: [] as Tag[], _catPath: [] as number[] })
const rules = { expense_type: [{ required: true }], category_id: [{ required: true }], asset_id: [{ required: true, message: '请选择关联资产', trigger: 'change' }], amount: [{ required: true }], expense_date: [{ required: true }] }

const categoryTree = computed(() => {
  return allCategories.value.filter(c => c.type === form.expense_type)
})

watch(() => form.expense_type, () => {
  // Clear category selection if selected category is not of the new type
  if (form.category_id) {
    const exists = categoryTree.value.some(c => c.id === form.category_id || findInTree(c, form.category_id!))
    if (!exists) {
      form.category_id = null
      form._catPath = []
    }
  }
})

function findInTree(node: Category, targetId: number): boolean {
  if (node.id === targetId) return true
  if (node.children?.length) {
    return node.children.some(c => findInTree(c, targetId))
  }
  return false
}

const monthSummary = ref<any>(null)
const filters = reactive({ type: '', month: '' })
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()


async function loadData() {
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
  const [res, mr] = await Promise.all([
    dailyExpensesApi.list(params),
    dailyExpensesApi.monthly(y, m)
  ])
  expenses.value = res.list
  total.value = res.total
  monthSummary.value = mr
}

function handleSearch() {
  page.value = 1
  loadData()
}

function handleSizeChange() {
  page.value = 1
  loadData()
}

function onCatChange(val: any) { const last = (val as any[])?.[(val as any[]).length - 1]; form.category_id = last != null ? Number(last) : null }

function openDialog(expense?: any) {
  editingId.value = expense?.id ?? null
  Object.assign(form, expense ? { expense_type: expense.expense_type, category_id: Number(expense.category_id), asset_id: Number(expense.asset_id), amount: Number(expense.amount), expense_date: expense.expense_date, note: expense.note, tags: expense.tags ?? [], _catPath: [Number(expense.category_id)] }
    : { expense_type: 'expense', category_id: null, asset_id: null, amount: 0, expense_date: new Date().toISOString().slice(0, 10), note: '', tags: [], _catPath: [] })
  dialogVisible.value = true
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data = {
        ...form,
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
  await ElMessageBox.confirm('确定删除该记录吗？', '提示', { type: 'warning' })
  await dailyExpensesApi.delete(expense.id)
  ElMessage.success('删除成功')
  loadData()
}

onMounted(async () => {
  try {
    const [, assetRes] = await Promise.all([
      categoryStore.loadCategories(),
      assetsApi.list({ page_size: 500 }),
    ])
    allCategories.value = categoryStore.incomeExpenseCategories
    allAssets.value = assetRes.list
    filters.month = new Date().toISOString().slice(0, 7)
    loadData()
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
  http.get('/export/daily-expenses', { responseType: 'blob' }).then((blob: unknown) => {
    const b = blob as Blob
    const url = URL.createObjectURL(b)
    const a = document.createElement('a')
    a.href = url
    const now = new Date().toISOString().slice(0, 10)
    a.download = `daily_expenses_${now}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
  }).catch(() => ElMessage.error('导出失败'))
}

function handleFileChange(raw: any) {
  const file = raw.raw ?? null
  importFile.value = file
  importText.value = null
  importResult.value = null
  if (file) {
    const reader = new FileReader()
    reader.onload = (e) => { importText.value = e.target?.result as string }
    reader.readAsText(file, 'UTF-8')
  }
}

async function handleImport() {
  if (!importText.value) return
  importing.value = true
  importResult.value = null
  try {
    const res = await dailyExpensesApi.importCsv(importText.value) as unknown as { imported: number; errors: number; errors_detail?: string }
    importResult.value = { imported: res.imported, errors: res.errors, errors_detail: res.errors_detail, ok: true }
    if (res.errors === 0) {
      ElMessage.success(`成功导入 ${res.imported} 条记录`)
      importDialogVisible.value = false
      importFile.value = null
      importText.value = null
      importResult.value = null
      loadData()
    }
  } catch (err: any) {
    importResult.value = { imported: 0, errors: 1, errors_detail: err?.message || '导入失败', ok: false }
  } finally {
    importing.value = false
  }
}
</script>

<style scoped>
.daily-expenses-page {
  padding: 24px;
  background-color: var(--mf-background);
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.action-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
  padding: 10px 20px;
  box-shadow: 0 0 12px rgba(0, 212, 255, 0.3);
  transition: all 0.2s ease;
}

.action-btn:hover {
  transform: translateY(-1px);
  box-shadow: var(--mf-shadow-glow);
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
  color: #94a3b8;
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
}

.summary-value {
  font-size: 24px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}

.income-text { color: #34d399; text-shadow: 0 0 8px rgba(52,211,153,0.4); }
.expense-text { color: #f87171; text-shadow: 0 0 8px rgba(248,113,113,0.3); }

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
  padding: 16px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  overflow: auto;
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table) {
  height: 100%;

  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table__body-wrapper) {
  overflow-y: auto;

  overflow-y: auto;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: rgba(0, 212, 255, 0.06);
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

:deep(.premium-header th) {
  background-color: rgba(0, 212, 255, 0.06) !important;
  color: #94a3b8 !important;
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
  background-color: rgba(0, 212, 255, 0.04) !important;
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
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: center;
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
  background: linear-gradient(135deg, #00d4ff, #0ea5e9) !important;
  border-color: #00d4ff !important;
  color: #060b18 !important;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.6) !important;
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

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.import-dialog-content { padding: 8px 0; }
.import-hint { margin: 0 0 16px; font-size: 13px; color: #94a3b8; line-height: 1.6; }
.import-upload { width: 100%; }
.import-upload :deep(.el-upload-dragger) { padding: 28px 0; border-radius: var(--mf-radius-md); border: 1px dashed var(--mf-border); background: var(--mf-background); }
.import-upload :deep(.el-upload-dragger:hover) { border-color: #00d4ff; }
.import-upload :deep(.el-icon--upload) { font-size: 40px; color: #00d4ff; margin-bottom: 12px; }
.import-result { margin-top: 16px; padding: 12px 16px; border-radius: var(--mf-radius-md); font-size: 13px; display: flex; flex-direction: column; gap: 4px; }
.import-result.success { background: rgba(16,185,129,0.1); border: 1px solid rgba(16,185,129,0.3); color: #34d399; }
.import-result.error { background: rgba(239,68,68,0.1); border: 1px solid rgba(239,68,68,0.3); color: #f87171; }
.import-result .el-icon { font-size: 16px; }
.import-errors { font-size: 12px; color: #fca5a5; white-space: pre-wrap; word-break: break-all; }

/* Constrain main content row to page height */
:deep(.daily-expenses-page > .el-row),
:deep(.transactions-page > .el-row) {
  flex: 1;
  min-height: 0;
}

:deep(.daily-expenses-page > .el-row > .el-col),
:deep(.transactions-page > .el-row > .el-col) {
  display: flex;
  flex-direction: column;
  min-height: 0;
}

/* Constrain layout to viewport */
.daily-expenses-page > .el-row {
  flex: 1;
  min-height: 0;
}
.daily-expenses-page > .el-row > .el-col {
  display: flex;
  flex-direction: column;
  min-height: 0;
}
.main-panel {
  flex: 1;
  min-height: 0;
  overflow: hidden;
}
</style>
