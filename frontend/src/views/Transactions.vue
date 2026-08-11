<template>
  <div class="transactions-page">
    <el-card>
      <template #header>
        <div class="header">
          <span>交易记录</span>
          <el-button type="primary" @click="openDialog()">
            <el-icon><Plus /></el-icon> 新增交易
          </el-button>
        </div>
      </template>

      <!-- 筛选 -->
      <el-form :inline="true" :model="filters" class="filters">
        <el-form-item label="资产">
          <el-select v-model="filters.asset_id" placeholder="全部" clearable style="width: 160px">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="String(a.id)" />
          </el-select>
        </el-form-item>
        <el-form-item label="类型">
          <el-select v-model="filters.type" placeholder="全部" clearable style="width: 120px">
            <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
          </el-select>
        </el-form-item>
        <el-form-item label="日期范围">
          <el-date-picker v-model="filters.dateRange" type="daterange" start-placeholder="开始" end-placeholder="结束" value-format="YYYY-MM-DD" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="loadData">查询</el-button>
          <el-button @click="resetFilters">重置</el-button>
        </el-form-item>
      </el-form>

      <!-- 表格 -->
      <el-table :data="transactions" stripe>
        <el-table-column prop="transaction_date" label="日期" width="120" />
        <el-table-column prop="asset_name" label="资产" />
        <el-table-column prop="transaction_type" label="类型" width="80">
          <template #default="{ row }">
            <el-tag :type="typeTag(row.transaction_type)" size="small">{{ typeLabel(row.transaction_type) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="amount" label="金额" width="120">
          <template #default="{ row }">{{ formatCurrency(row.amount) }}</template>
        </el-table-column>
        <el-table-column prop="quantity" label="数量" width="80" />
        <el-table-column prop="price_per_unit" label="单价" width="100">
          <template #default="{ row }">{{ row.price_per_unit ? formatCurrency(row.price_per_unit) : '-' }}</template>
        </el-table-column>
        <el-table-column prop="note" label="备注" />
        <el-table-column label="操作" width="120">
          <template #default="{ row }">
            <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
            <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <!-- 对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑交易' : '新增交易'" width="500px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
        <el-form-item label="资产" prop="asset_id">
          <el-select v-model="form.asset_id" placeholder="选择资产" style="width: 100%">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="a.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="交易类型" prop="transaction_type">
          <el-select v-model="form.transaction_type" style="width: 100%">
            <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
          </el-select>
        </el-form-item>
        <el-form-item label="金额" prop="amount">
          <el-input-number v-model="form.amount" :precision="2" :min="0" style="width: 100%" />
        </el-form-item>
        <el-form-item label="数量">
          <el-input-number v-model="form.quantity" :precision="4" :min="0" style="width: 100%" />
        </el-form-item>
        <el-form-item label="单价">
          <el-input-number v-model="form.price_per_unit" :precision="4" :min="0" style="width: 100%" />
        </el-form-item>
        <el-form-item label="日期" prop="transaction_date">
          <el-date-picker v-model="form.transaction_date" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
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
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import type { Transaction, Asset } from '@/types'

const transactions = ref<Transaction[]>([])
const assets = ref<Asset[]>([])
const filters = reactive({ asset_id: '', type: '', dateRange: null as string[] | null })
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const transactionTypes = [
  { label: '存入', value: 'deposit' }, { label: '取出', value: 'withdrawal' },
  { label: '买入', value: 'buy' }, { label: '卖出', value: 'sell' },
  { label: '转入', value: 'transfer_in' }, { label: '转出', value: 'transfer_out' },
  { label: '手续费', value: 'fee' }, { label: '收益', value: 'income' },
  { label: '亏损', value: 'loss' },
]

const form = reactive({ asset_id: null as number | null, transaction_type: 'buy' as Transaction['transaction_type'], amount: 0, quantity: 0, price_per_unit: 0, transaction_date: '', note: '' })
const rules = { asset_id: [{ required: true }], transaction_type: [{ required: true }], amount: [{ required: true }], transaction_date: [{ required: true }] }

function typeLabel(t: string) { return transactionTypes.find(x => x.value === t)?.label || t }
function typeTag(t: string): 'success' | 'warning' | 'danger' | 'info' | 'primary' {
  const map: Record<string, 'success' | 'warning' | 'danger' | 'info' | 'primary'> = { buy: 'success', sell: 'warning', deposit: 'success', withdrawal: 'danger', income: 'success', loss: 'danger' }
  return map[t] || 'info'
}
function formatCurrency(v: number) { return new Intl.NumberFormat('zh-CN', { style: 'currency', currency: 'CNY' }).format(v) }

async function loadData() {
  const params: any = {}
  if (filters.asset_id) params.asset_id = filters.asset_id
  if (filters.type) params.type = filters.type
  if (filters.dateRange?.[0]) params.start_date = filters.dateRange[0]
  if (filters.dateRange?.[1]) params.end_date = filters.dateRange[1]
  const res = await transactionsApi.list(params)
  transactions.value = res.data
}

function resetFilters() { Object.assign(filters, { asset_id: '', type: '', dateRange: null }) ; loadData() }

function openDialog(txn?: any) {
  editingId.value = txn?.id ?? null
  Object.assign(form, txn ? { asset_id: txn.asset_id, transaction_type: txn.transaction_type, amount: txn.amount, quantity: txn.quantity ?? 0, price_per_unit: txn.price_per_unit ?? 0, transaction_date: txn.transaction_date, note: txn.note }
    : { asset_id: null, transaction_type: 'buy', amount: 0, quantity: 0, price_per_unit: 0, transaction_date: '', note: '' })
  dialogVisible.value = true
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      if (editingId.value) { await transactionsApi.update(editingId.value, form) }
      else { await transactionsApi.create(form) }
      ElMessage.success('保存成功')
      dialogVisible.value = false
      loadData()
    } finally { saving.value = false }
  })
}

async function handleDelete(txn: any) {
  await ElMessageBox.confirm(`确定删除该交易记录吗？`, '提示', { type: 'warning' })
  await transactionsApi.delete(txn.id)
  ElMessage.success('删除成功')
  loadData()
}

onMounted(async () => {
  const res = await assetsApi.list()
  assets.value = res.data
  loadData()
})
</script>

<style scoped>
.transactions-page { }
.header { display: flex; justify-content: space-between; align-items: center; }
.filters { margin-bottom: 16px; }
</style>
