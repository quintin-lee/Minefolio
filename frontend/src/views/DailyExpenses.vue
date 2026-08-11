<template>
  <div class="daily-expenses-page">
    <el-row :gutter="16">
      <!-- 左侧：记账列表 -->
      <el-col :span="16">
        <el-card>
          <template #header>
            <div class="header">
              <span>日常收支</span>
              <el-button type="primary" @click="openDialog()">
                <el-icon><Plus /></el-icon> 新增
              </el-button>
            </div>
          </template>

          <el-form :inline="true" class="filters">
            <el-form-item label="类型">
              <el-select v-model="filters.type" placeholder="全部" clearable>
                <el-option label="收入" value="income" />
                <el-option label="支出" value="expense" />
              </el-select>
            </el-form-item>
            <el-form-item label="月份">
              <el-date-picker v-model="filters.month" type="month" placeholder="选择月份" value-format="YYYY-MM" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="loadData">查询</el-button>
            </el-form-item>
          </el-form>

          <!-- 月度汇总 -->
          <el-row :gutter="12" class="monthly-summary">
            <el-col :span="8">
              <el-statistic title="收入" :value="monthSummary?.total_income ?? 0" :precision="2" prefix="¥" value-style="color: #67c23a" />
            </el-col>
            <el-col :span="8">
              <el-statistic title="支出" :value="monthSummary?.total_expense ?? 0" :precision="2" prefix="¥" value-style="color: #f56c6c" />
            </el-col>
            <el-col :span="8">
              <el-statistic title="结余" :value="monthSummary?.balance ?? 0" :precision="2" prefix="¥" value-style="color: #409eff" />
            </el-col>
          </el-row>

          <el-table :data="expenses" stripe style="margin-top: 12px">
            <el-table-column prop="expense_date" label="日期" width="110" />
            <el-table-column prop="category_name" label="分类" />
            <el-table-column prop="expense_type" label="类型" width="60">
              <template #default="{ row }">
                <el-tag :type="row.expense_type === 'income' ? 'success' : 'danger'" size="small">
                  {{ row.expense_type === 'income' ? '收' : '支' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="amount" label="金额" width="100">
              <template #default="{ row }">
                <span :class="row.expense_type === 'income' ? 'income-text' : 'expense-text'">
                  {{ row.expense_type === 'income' ? '+' : '-' }}{{ formatCurrency(row.amount) }}
                </span>
              </template>
            </el-table-column>
            <el-table-column prop="note" label="备注" />
            <el-table-column label="标签" width="120">
              <template #default="{ row }">
                <el-tag v-for="tag in (row as any).tags" :key="tag.id" :color="tag.color" size="small" style="margin-right: 4px">
                  {{ tag.name }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作" width="100">
              <template #default="{ row }">
                <el-button link type="primary" @click="openDialog(row as any)">编辑</el-button>
                <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>

      <!-- 右侧：月度图表 -->
      <el-col :span="8">
        <el-card>
          <template #header>月度收支趋势</template>
          <MonthlyChart :data="monthSummary" />
        </el-card>
        <el-card style="margin-top: 16px">
          <template #header>分类占比</template>
          <ExpenseCategoryPie :data="monthSummary?.by_category ?? []" />
        </el-card>
      </el-col>
    </el-row>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑收支' : '新增收支'" width="480px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px">
        <el-form-item label="类型" prop="expense_type">
          <el-radio-group v-model="form.expense_type">
            <el-radio value="income">收入</el-radio>
            <el-radio value="expense">支出</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :options="categoryTree" :props="{ checkStrictly: true, value: 'id', label: 'name' }" placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>
        <el-form-item label="金额" prop="amount">
          <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" />
        </el-form-item>
        <el-form-item label="日期" prop="expense_date">
          <el-date-picker v-model="form.expense_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
        </el-form-item>
        <el-form-item label="标签">
          <TagPicker v-model="form.tags" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" type="textarea" :rows="2" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="handleSubmit">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { categoriesApi } from '@/api/categories'
import TagPicker from '@/components/TagPicker.vue'
import MonthlyChart from '@/components/MonthlyChart.vue'
import ExpenseCategoryPie from '@/components/ExpenseCategoryPie.vue'
import type { DailyExpense, Tag, Category } from '@/types'

const expenses = ref<DailyExpense[]>([])
const categoryTree = ref<Category[]>([])
const monthSummary = ref<any>(null)
const filters = reactive({ type: '', month: '' })
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const form = reactive({ expense_type: 'expense' as 'income' | 'expense', category_id: null as number | null, amount: 0, expense_date: '', note: '', tags: [] as Tag[], _catPath: [] as number[] })
const rules = { expense_type: [{ required: true }], category_id: [{ required: true }], amount: [{ required: true }], expense_date: [{ required: true }] }

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
  expenses.value = res.data
  if (filters.month) {
    const [y = '', m = ''] = filters.month.split('-')
    const mr = await dailyExpensesApi.monthly(parseInt(y), parseInt(m))
    monthSummary.value = mr.data
  }
}

function onCatChange(val: any) { form.category_id = (val as number[])?.[(val as number[]).length - 1] ?? null }

function openDialog(expense?: any) {
  editingId.value = expense?.id ?? null
  Object.assign(form, expense ? { expense_type: expense.expense_type, category_id: expense.category_id, amount: expense.amount, expense_date: expense.expense_date, note: expense.note, tags: expense.tags ?? [], _catPath: [expense.category_id] }
    : { expense_type: 'expense', category_id: null, amount: 0, expense_date: new Date().toISOString().slice(0, 10), note: '', tags: [], _catPath: [] })
  dialogVisible.value = true
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data = { ...form, tags: form.tags.map(t => ({ id: t.id })) }
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
  const res = await categoriesApi.list()
  categoryTree.value = res.data
  filters.month = new Date().toISOString().slice(0, 7)
  loadData()
})
</script>

<style scoped>
.daily-expenses-page { }
.header { display: flex; justify-content: space-between; align-items: center; }
.filters { margin-bottom: 12px; }
.monthly-summary { margin-bottom: 12px; }
.income-text { color: #67c23a; font-weight: bold; }
.expense-text { color: #f56c6c; font-weight: bold; }
</style>
