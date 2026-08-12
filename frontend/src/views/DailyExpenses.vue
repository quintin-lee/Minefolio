<template>
  <div class="daily-expenses-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>日常收支</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog()">
        <el-icon><Plus /></el-icon> 新增记录
      </el-button>
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
                <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
              </el-form-item>
            </el-form>
          </div>

          <!-- 月度汇总 -->
          <el-row :gutter="16" class="monthly-summary">
            <el-col :span="8">
              <div class="summary-card">
                <div class="summary-label">本月收入</div>
                <div class="summary-value income-text">{{ formatCurrency(monthSummary?.total_income ?? 0) }}</div>
              </div>
            </el-col>
            <el-col :span="8">
              <div class="summary-card">
                <div class="summary-label">本月支出</div>
                <div class="summary-value expense-text">{{ formatCurrency(monthSummary?.total_expense ?? 0) }}</div>
              </div>
            </el-col>
            <el-col :span="8">
              <div class="summary-card highlight-card">
                <div class="summary-label">本月结余</div>
                <div class="summary-value">{{ formatCurrency(monthSummary?.balance ?? 0) }}</div>
              </div>
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
              <span style="float: right; color: #8492a6; font-size: 13px">{{ a.currency }} {{ Number(a.current_value).toFixed(2) }}</span>
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
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { categoriesApi } from '@/api/categories'
import { assetsApi } from '@/api/assets'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import type { DailyExpense, Tag, Category, Asset } from '@/types'

const expenses = ref<DailyExpense[]>([])
const allCategories = ref<Category[]>([])
const allAssets = ref<Asset[]>([])

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

function formatCurrency(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v) }

async function loadData() {
  const params: any = {}
  if (filters.type) params.expense_type = filters.type
  if (filters.month) {
    const [y = '', m = ''] = filters.month.split('-')
    params.start_date = `${y}-${m}-01`
    const lastDay = new Date(parseInt(y), parseInt(m), 0).getDate()
    params.end_date = `${y}-${m}-${String(lastDay).padStart(2, '0')}`
  }
  const res = await dailyExpensesApi.list(params)
  expenses.value = res
  if (filters.month) {
    const [y = '', m = ''] = filters.month.split('-')
    const mr = await dailyExpensesApi.monthly(parseInt(y), parseInt(m))
    monthSummary.value = mr
  }
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
  const [catRes, assetRes] = await Promise.all([
    categoriesApi.list({ type: 'income,expense' }),
    assetsApi.list(),
  ])
  allCategories.value = catRes
  allAssets.value = assetRes
  filters.month = new Date().toISOString().slice(0, 7)
  loadData()
})
</script>

<style scoped>
.daily-expenses-page {
  padding: 24px;
  background-color: #f8fafc;
  min-height: 100%;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 24px;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #3b82f6 0%, #2563eb 100%);
  border-radius: 4px;
}

.header-title h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 600;
  color: #1e293b;
  letter-spacing: 0.5px;
}

.action-btn {
  border-radius: 8px;
  font-weight: 500;
  padding: 10px 20px;
  box-shadow: 0 4px 6px -1px rgba(59, 130, 246, 0.2);
  transition: all 0.2s ease;
}

.action-btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 8px -1px rgba(59, 130, 246, 0.3);
}

.filter-panel {
  background: #ffffff;
  border-radius: 16px;
  padding: 20px 24px;
  margin-bottom: 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
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
  color: #475569;
}

.filter-actions {
  margin-left: auto;
}

.search-btn {
  border-radius: 8px;
}

.monthly-summary {
  margin-bottom: 24px;
}

.summary-card {
  background: #ffffff;
  border-radius: 16px;
  padding: 20px 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
  display: flex;
  flex-direction: column;
  gap: 8px;
  transition: transform 0.2s ease;
}

.summary-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.06);
}

.highlight-card {
  background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
}

.highlight-card .summary-label {
  color: #94a3b8;
}

.highlight-card .summary-value {
  color: #ffffff;
}

.summary-label {
  font-size: 14px;
  color: #64748b;
  font-weight: 500;
}

.summary-value {
  font-size: 24px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}

.income-text { color: #10b981; }
.expense-text { color: #ef4444; }

.table-container {
  background: #ffffff;
  border-radius: 16px;
  padding: 16px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: #f8fafc;
}

:deep(.premium-header th) {
  background-color: #f8fafc !important;
  color: #64748b;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: none !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid #f1f5f9;
  padding: 16px 0;
}

:deep(.premium-row:hover > td) {
  background-color: #f8fafc !important;
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
  background: #ffffff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.panel-title {
  margin: 0 0 20px 0;
  font-size: 16px;
  font-weight: 600;
  color: #1e293b;
}

/* Dialog Styles */
:deep(.premium-dialog) {
  border-radius: 16px;
  overflow: hidden;
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
}

:deep(.premium-dialog .el-dialog__header) {
  margin: 0;
  padding: 24px;
  border-bottom: 1px solid #f1f5f9;
  background: #ffffff;
}

:deep(.premium-dialog .el-dialog__title) {
  font-weight: 600;
  font-size: 18px;
  color: #1e293b;
}

:deep(.premium-dialog .el-dialog__body) {
  padding: 32px 24px;
  background: #f8fafc;
}

:deep(.premium-dialog .el-dialog__footer) {
  padding: 16px 24px;
  border-top: 1px solid #f1f5f9;
  background: #ffffff;
  margin: 0;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.type-radio-group :deep(.el-radio-button__inner) {
  border-radius: 8px !important;
  margin-right: 12px;
  border: 1px solid #e2e8f0;
  box-shadow: none !important;
  padding: 10px 24px;
  font-weight: 500;
}

.type-radio-group :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background-color: #3b82f6;
  border-color: #3b82f6;
  color: white;
}

.premium-form :deep(.el-input__wrapper) {
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
  border-radius: 8px;
  padding: 6px 12px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}

.cancel-btn {
  border-radius: 8px;
  font-weight: 500;
}

.save-btn {
  border-radius: 8px;
  font-weight: 500;
  padding: 8px 24px;
}
</style>
