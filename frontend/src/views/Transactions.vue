<template>
  <div class="transactions-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>交易记录</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog()">
        <el-icon><Plus /></el-icon> 新增交易
      </el-button>
    </div>

    <!-- 筛选 -->
    <div class="filter-panel">
      <el-form :inline="true" :model="filters" class="premium-filters">
        <el-form-item label="资产">
          <el-select v-model="filters.asset_id" placeholder="全部" clearable class="filter-select">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="String(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="类型">
          <el-select v-model="filters.type" placeholder="全部" clearable class="filter-select type-select">
            <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
          </el-select>
        </el-form-item>
        <el-form-item label="日期范围">
          <el-date-picker v-model="filters.dateRange" type="daterange" start-placeholder="开始日期" end-placeholder="结束日期" value-format="YYYY-MM-DD" class="filter-date" />
        </el-form-item>
        <el-form-item class="filter-actions">
          <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
          <el-button class="reset-btn" @click="resetFilters">重置</el-button>
        </el-form-item>
      </el-form>
    </div>

    <!-- 表格 -->
    <div class="table-container">
      <el-table :data="transactions" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
        <el-table-column prop="transaction_date" label="日期" width="120" />
        <el-table-column prop="asset_name" label="资产" min-width="120" />
         <el-table-column prop="transaction_type" label="类型" width="100">
           <template #default="{ row }">
             <el-tag :type="typeTag(row.transaction_type)" effect="light" class="type-badge" round>
               {{ typeLabel(row.transaction_type) }}
             </el-tag>
           </template>
         </el-table-column>
         <el-table-column prop="source_type" label="收支" width="80">
           <template #default="{ row }">
             <el-tag :type="row.source_type === 'income' ? 'success' : 'danger'" effect="light" round>
               {{ row.source_type === 'income' ? '收入' : '支出' }}
             </el-tag>
           </template>
         </el-table-column>
        <el-table-column prop="amount" label="金额" min-width="140" align="right">
          <template #default="{ row }">
            <span :class="['mono-amount', {'success': 'income-text', 'warning': 'warning-text', 'danger': 'expense-text', 'info': '', 'primary': ''}[typeTag(row.transaction_type)]]">
              {{ formatCurrency(row.amount) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="quantity" label="数量" width="100" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ row.quantity || '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="price_per_unit" label="单价" width="120" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ row.price_per_unit ? formatCurrency(row.price_per_unit) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="category_name" label="分类" min-width="100">
          <template #default="{ row }">
            <span v-if="row.category_name">{{ row.category_name }}</span>
            <span v-else class="muted-text">—</span>
          </template>
        </el-table-column>
        <el-table-column prop="note" label="备注" min-width="150" />
        <el-table-column label="操作" width="120" align="center">
          <template #default="{ row }">
            <div class="action-buttons">
              <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
              <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <!-- 对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑交易' : '新增交易'" width="520px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-alert v-if="editingId" type="info" :closable="false" show-icon title="修改金额/类型将同步调整关联资产的余额" style="margin-bottom: 16px" />
        <el-form-item label="资产" prop="asset_id">
          <el-select v-model="form.asset_id" placeholder="选择资产" style="width: 100%" @change="onAssetChange">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="a.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="币种" prop="currency">
          <el-select v-model="form.currency" placeholder="自动" clearable style="width: 100%">
            <el-option v-for="cur in currencyOptions" :key="cur" :label="cur" :value="cur" />
          </el-select>
        </el-form-item>
         <el-form-item label="交易类型" prop="transaction_type">
           <el-select v-model="form.transaction_type" style="width: 100%" @change="onTransactionTypeChange">
             <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
           </el-select>
         </el-form-item>
         <el-form-item label="收支类型" prop="source_type">
           <el-radio-group v-model="form.source_type">
             <el-radio value="expense">支出</el-radio>
             <el-radio value="income">收入</el-radio>
           </el-radio-group>
         </el-form-item>
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :options="categoryTree" :props="{ checkStrictly: true, value: 'id', label: 'name' }" placeholder="选择分类" style="width: 100%" clearable />
        </el-form-item>
        <div class="form-row">
          <el-form-item label="金额" prop="amount" style="flex: 1">
            <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" :controls="false" />
          </el-form-item>
        </div>
        <div class="form-row">
          <el-form-item label="数量" style="flex: 1">
            <el-input-number v-model="form.quantity" :precision="4" :min="0" style="width: 100%" :controls="false" />
          </el-form-item>
          <el-form-item label="单价" style="flex: 1">
            <el-input-number v-model="form.price_per_unit" :precision="4" :min="0" style="width: 100%" :controls="false" />
          </el-form-item>
        </div>
        <el-form-item label="日期" prop="transaction_date">
          <el-date-picker v-model="form.transaction_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
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
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { categoriesApi } from '@/api/categories'
import { formatCurrency } from '@/utils/format'
import type { Transaction, Asset, Category } from '@/types'

const transactions = ref<Transaction[]>([])
const assets = ref<Asset[]>([])
const allCategories = ref<Category[]>([])
const filters = reactive({ asset_id: '', type: '', dateRange: null as string[] | null })
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const CURRENCIES = ['CNY', 'USD', 'EUR', 'GBP', 'JPY', 'HKD', 'KRW', 'TWD', 'SGD', 'AUD', 'CAD'] as const
const currencyOptions = [...CURRENCIES]

const categoryTree = computed(() => allCategories.value)

const transactionTypes = [
  { label: '存入', value: 'deposit' }, { label: '取出', value: 'withdrawal' },
  { label: '买入', value: 'buy' }, { label: '卖出', value: 'sell' },
  { label: '转入', value: 'transfer_in' }, { label: '转出', value: 'transfer_out' },
  { label: '手续费', value: 'fee' }, { label: '收益', value: 'income' },
  { label: '亏损', value: 'loss' },
]
const form = reactive({ asset_id: null as number | null, transaction_type: 'buy' as Transaction['transaction_type'], source_type: 'expense' as 'income' | 'expense', amount: 0, quantity: 0, price_per_unit: 0, transaction_date: '', note: '', category_id: null as number | null, currency: '' as string, _catPath: [] as number[] })
const rules = { asset_id: [{ required: true, message: '请选择资产' }], transaction_type: [{ required: true, message: '请选择交易类型' }], amount: [{ required: true, message: '请输入金额' }, { type: 'number', min: Number.EPSILON, message: '金额必须大于零' }], transaction_date: [{ required: true, message: '请选择日期' }] }

function typeLabel(t: string) { return transactionTypes.find(x => x.value === t)?.label || t }
function typeTag(t: string): 'success' | 'warning' | 'danger' | 'info' | 'primary' {
  const map: Record<string, 'success' | 'warning' | 'danger' | 'info' | 'primary'> = { buy: 'success', sell: 'warning', deposit: 'success', withdrawal: 'danger', income: 'success', loss: 'danger' }
  return map[t] || 'info'
}

async function loadData() {
  const params: any = {}
  if (filters.asset_id) params.asset_id = filters.asset_id
  if (filters.type) params.type = filters.type
  if (filters.dateRange?.[0]) params.start_date = filters.dateRange[0]
  if (filters.dateRange?.[1]) params.end_date = filters.dateRange[1]
  const res = await transactionsApi.list(params)
  transactions.value = res
}

function onAssetChange(assetId: number | null) {
  const asset = assets.value.find(a => a.id === assetId)
  if (asset) form.currency = asset.currency || ''
}

function onTransactionTypeChange(type: string) {
  // 根据交易类型自动推断收支方向
  const incomeTypes = ['deposit', 'income', 'sell', 'transfer_in']
  form.source_type = incomeTypes.includes(type) ? 'income' : 'expense'
  // 同步更新分类树（收入显示收入分类，支出显示支出分类）
  if (form.source_type === 'income') {
    // 保持当前分类如果已经是收入分类
  } else {
    // 保持当前分类如果已经是支出分类
  }
}

function inferSourceType(type: string): 'income' | 'expense' {
  const incomeTypes = ['deposit', 'income', 'sell', 'transfer_in']
  return incomeTypes.includes(type) ? 'income' : 'expense'
}

function onCatChange(last: number | null) {
  form.category_id = last !== null ? Number(last) : null
}

function resetFilters() { Object.assign(filters, { asset_id: '', type: '', dateRange: null }) ; loadData() }

function openDialog(txn?: any) {
  editingId.value = txn?.id ?? null
  const cur = txn?.currency ? String(txn.currency) : ''
  const srcType = txn?.source_type || inferSourceType(txn?.transaction_type || 'buy')
  Object.assign(form, txn ? {
    asset_id: Number(txn.asset_id),
    transaction_type: txn.transaction_type,
    source_type: srcType,
    amount: Number(txn.amount),
    quantity: Number(txn.quantity) ?? 0,
    price_per_unit: Number(txn.price_per_unit) ?? 0,
    transaction_date: txn.transaction_date,
    note: txn.note || '',
    category_id: Number(txn.category_id) || null,
    currency: cur || '',
    _catPath: [Number(txn.category_id)].filter(Boolean),
  } : {
    asset_id: null, transaction_type: 'buy', source_type: 'expense', amount: 0, quantity: 0, price_per_unit: 0,
    transaction_date: new Date().toISOString().slice(0, 10), note: '',
    category_id: null, currency: '', _catPath: [],
  })
  dialogVisible.value = true
}

async function handleSubmit() {
  // 前置检查：确保必要字段有值（防止 ElForm validate 回调时序问题导致空字段发出）
  if (!form.asset_id || !form.transaction_type || !form.amount || form.amount <= 0 || !form.transaction_date) {
    ElMessage.error('请填写完整的交易信息')
    return
  }
  try {
    await formRef.value.validate()
  } catch {
    return
  }
  saving.value = true
  try {
    if (editingId.value) { await transactionsApi.update(editingId.value, form) }
    else { await transactionsApi.create(form) }
    ElMessage.success('保存成功')
    dialogVisible.value = false
    loadData()
  } finally { saving.value = false }
}

async function handleDelete(txn: any) {
  await ElMessageBox.confirm(`确定删除该交易记录吗？`, '提示', { type: 'warning' })
  await transactionsApi.delete(txn.id)
  ElMessage.success('删除成功')
  loadData()
}

onMounted(async () => {
  const [assetsRes, catsRes] = await Promise.all([assetsApi.list(), categoriesApi.list({ type: 'income,expense' })])
  assets.value = assetsRes
  allCategories.value = catsRes
  loadData()
})
</script>

<style scoped>
.transactions-page {
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

.search-btn, .reset-btn {
  border-radius: 8px;
}

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

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: #475569;
}

.income-text { color: #10b981; }
.expense-text { color: #ef4444; }
.warning-text { color: #f59e0b; }

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: center;
}

.form-row {
  display: flex;
  gap: 16px;
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
