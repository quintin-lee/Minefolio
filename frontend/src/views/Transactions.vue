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

    <!-- 顶部汇总统计卡片 -->
    <el-row :gutter="16" class="summary-cards">
      <el-col :xs="12" :sm="6">
        <div class="summary-card">
          <div class="summary-label">本月交易总额</div>
          <div class="summary-value font-mono text-primary">{{ formatCurrency(monthlyTotalVolume) }}</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="summary-card">
          <div class="summary-label">买入/存入合计</div>
          <div class="summary-value font-mono income-text">+{{ formatCurrency(monthlyInflows) }}</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="summary-card">
          <div class="summary-label">卖出/取出合计</div>
          <div class="summary-value font-mono expense-text">-{{ formatCurrency(monthlyOutflows) }}</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="summary-card highlight-card">
          <div class="summary-label">本月交易笔数</div>
          <div class="summary-value font-mono">{{ monthlyCount }} 笔</div>
        </div>
      </el-col>
    </el-row>

    <!-- 筛选与表格主面板 -->
    <div class="main-panel">
      <div class="filter-panel">
        <el-form :inline="true" :model="filters" class="premium-filters">
          <el-form-item label="资产账户">
            <el-select v-model="filters.asset_id" placeholder="全部资产" clearable class="filter-select">
              <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="String(a.id)" />
            </el-select>
          </el-form-item>
          <el-form-item label="交易分类">
            <el-cascader
              v-model="filters.category_id"
              :options="categoryTree"
              :props="{ checkStrictly: true, value: 'id', label: 'name', emitPath: false }"
              placeholder="全部分类"
              clearable
              class="filter-select"
            />
          </el-form-item>
          <el-form-item label="交易类型">
            <el-select v-model="filters.type" placeholder="全部类型" clearable class="filter-select">
              <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
            </el-select>
          </el-form-item>
          <el-form-item label="日期范围">
            <el-date-picker
              v-model="filters.dateRange"
              type="daterange"
              start-placeholder="开始日期"
              end-placeholder="结束日期"
              value-format="YYYY-MM-DD"
              class="filter-date"
            />
          </el-form-item>
          <el-form-item class="filter-actions">
            <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
            <el-button class="reset-btn" @click="resetFilters">重置</el-button>
          </el-form-item>
        </el-form>
      </div>

      <!-- 表格记录视图 -->
      <div class="table-container">
        <el-table
          v-loading="loading"
          :data="transactions"
          class="premium-table"
          row-class-name="premium-row"
          header-cell-class-name="premium-header"
        >
          <el-table-column prop="transaction_date" label="日期" width="120" />
          <el-table-column prop="asset_name" label="账户资产" min-width="130" />
          <el-table-column prop="linked_asset_name" label="扣款/回流账户" min-width="130">
            <template #default="{ row }">
              <el-tag v-if="row.linked_asset_name" size="small" type="warning" effect="light" class="type-badge" round>
                {{ row.linked_asset_name }}
              </el-tag>
              <span v-else class="muted-text">—</span>
            </template>
          </el-table-column>
          <el-table-column prop="transaction_type" label="交易类型" width="110">
            <template #default="{ row }">
              <el-tag :type="typeTag(row.transaction_type)" effect="light" class="type-badge" round>
                {{ typeLabel(row.transaction_type) }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="category_name" label="交易分类" min-width="120">
            <template #default="{ row }">
              <span v-if="row.category_name" class="category-pill">{{ row.category_name }}</span>
              <span v-else class="muted-text">—</span>
            </template>
          </el-table-column>
          <el-table-column prop="amount" label="交易金额" width="150" align="right">
            <template #default="{ row }">
              <span :class="['mono-amount', isIncomeType(row.transaction_type) ? 'income-text' : 'expense-text']">
                {{ isIncomeType(row.transaction_type) ? '+' : '-' }}{{ formatCurrency(row.amount) }}
              </span>
            </template>
          </el-table-column>
          <el-table-column label="单价 × 数量" min-width="150" align="center">
            <template #default="{ row }">
              <div v-if="row.quantity > 0 || row.price_per_unit > 0" class="trade-detail font-mono">
                <span class="price-tag">{{ formatCurrency(row.price_per_unit || 0) }}</span>
                <span class="times">×</span>
                <span class="qty-tag">{{ row.quantity || 0 }}</span>
              </div>
              <span v-else class="muted-text">—</span>
            </template>
          </el-table-column>
          <el-table-column prop="currency" label="币种" width="90" align="center">
            <template #default="{ row }">
              <el-tag size="small" type="info" effect="plain">{{ row.currency || 'CNY' }}</el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="note" label="备注" min-width="160">
            <template #default="{ row }">
              <span>{{ row.note || '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="120" align="center" fixed="right">
            <template #default="{ row }">
              <div class="action-buttons">
                <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
                <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
              </div>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>

    <!-- 对话框：新增/编辑交易 -->
    <el-dialog
      v-model="dialogVisible"
      :title="editingId ? '编辑交易记录' : '新增交易记录'"
      width="540px"
      class="premium-dialog"
      destroy-on-close
    >
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form" label-position="top">
        <el-alert
          v-if="editingId"
          type="info"
          :closable="false"
          show-icon
          title="修改金额/类型将自动联动计算并更新关联资产的账户余额"
          style="margin-bottom: 16px"
        />

        <el-row :gutter="16">
          <el-col :span="14">
            <el-form-item label="资产账户" prop="asset_id">
              <el-select v-model="form.asset_id" placeholder="选择资产账户" style="width: 100%" @change="onAssetChange">
                <el-option
                  v-for="a in assets"
                  :key="a.id"
                  :label="`${a.name} (${formatCurrency(a.current_value)})`"
                  :value="a.id"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="10">
            <el-form-item label="结算币种" prop="currency">
              <el-select v-model="form.currency" placeholder="默认 CNY" style="width: 100%">
                <el-option v-for="cur in currencyOptions" :key="cur" :label="cur" :value="cur" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :span="24">
            <el-form-item label="资金账户 (扣款/回流账户)" prop="linked_asset_id">
              <el-select v-model="form.linked_asset_id" placeholder="选择资金账户（买入从该账户扣款，卖出回流到该账户，可选）" clearable style="width: 100%" filterable>
                <el-option
                  v-for="a in assets"
                  :key="a.id"
                  :disabled="a.id === form.asset_id"
                  :label="`${a.name} (${formatCurrency(a.current_value)})`"
                  :value="a.id"
                />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="交易类型" prop="transaction_type">
              <el-select v-model="form.transaction_type" style="width: 100%" @change="onTransactionTypeChange">
                <el-option v-for="t in transactionTypes" :key="t.value" :label="t.label" :value="t.value" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="交易分类" prop="category_id">
              <el-cascader
                v-model="form._catPath"
                :options="categoryTree"
                :props="{ checkStrictly: true, value: 'id', label: 'name' }"
                placeholder="选择交易分类"
                style="width: 100%"
                clearable
                @change="onCatChange"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <!-- 仅买入/卖出时动态展开单价与数量 -->
        <el-row v-if="isTradingType(form.transaction_type)" :gutter="16" class="trading-fields">
          <el-col :span="12">
            <el-form-item label="交易单价">
              <el-input-number
                v-model="form.price_per_unit"
                :precision="4"
                :min="0"
                style="width: 100%"
                :controls="false"
                placeholder="0.0000"
                @input="calculateAmount"
              />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="交易数量">
              <el-input-number
                v-model="form.quantity"
                :precision="4"
                :min="0"
                style="width: 100%"
                :controls="false"
                placeholder="0.0000"
                @input="calculateAmount"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="交易金额" prop="amount">
              <el-input-number
                v-model="form.amount"
                :precision="2"
                :min="0.01"
                style="width: 100%"
                :controls="false"
                placeholder="0.00"
              />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="交易日期" prop="transaction_date">
              <el-date-picker
                v-model="form.transaction_date"
                type="date"
                value-format="YYYY-MM-DD"
                style="width: 100%"
                placeholder="选择日期"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="备注说明">
          <el-input v-model="form.note" type="textarea" :rows="2" placeholder="添加交易备注说明..." />
        </el-form-item>
      </el-form>

      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" class="save-btn" :loading="saving" @click="handleSubmit">
            保存记录
          </el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import { transactionsApi } from '@/api/transactions'
import { assetsApi } from '@/api/assets'
import { categoriesApi } from '@/api/categories'
import { formatCurrency } from '@/utils/format'
import type { Transaction, Asset, Category } from '@/types'

const loading = ref(false)
const saving = ref(false)
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const formRef = ref()

const transactions = ref<Transaction[]>([])
const assets = ref<Asset[]>([])
const allCategories = ref<Category[]>([])

const CURRENCIES = ['CNY', 'USD', 'EUR', 'GBP', 'JPY', 'HKD', 'KRW', 'TWD', 'SGD', 'AUD', 'CAD'] as const
const currencyOptions = [...CURRENCIES]

const transactionTypes = [
  { label: '存入', value: 'deposit' },
  { label: '取出', value: 'withdrawal' },
  { label: '买入', value: 'buy' },
  { label: '卖出', value: 'sell' },
  { label: '转入', value: 'transfer_in' },
  { label: '转出', value: 'transfer_out' },
  { label: '手续费', value: 'fee' },
  { label: '收益', value: 'income' },
  { label: '亏损', value: 'loss' },
]

const filters = reactive({
  asset_id: '',
  category_id: '' as string | number,
  type: '',
  dateRange: null as string[] | null,
})

const form = reactive({
  asset_id: null as number | null,
  linked_asset_id: null as number | null,
  transaction_type: 'deposit' as Transaction['transaction_type'],
  amount: 0,
  quantity: 0,
  price_per_unit: 0,
  transaction_date: new Date().toISOString().slice(0, 10),
  note: '',
  category_id: null as number | null,
  currency: 'CNY' as string,
  _catPath: [] as number[],
})

const rules = {
  asset_id: [{ required: true, message: '请选择资产账户' }],
  transaction_type: [{ required: true, message: '请选择交易类型' }],
  amount: [
    { required: true, message: '请输入交易金额' },
    { type: 'number', min: 0.01, message: '交易金额必须大于零' },
  ],
  transaction_date: [{ required: true, message: '请选择交易日期' }],
}

const categoryTree = computed(() => allCategories.value)

const currentMonthPrefix = computed(() => new Date().toISOString().slice(0, 7))

const monthlyTransactions = computed(() => {
  return transactions.value.filter(
    t => t.transaction_date && t.transaction_date.startsWith(currentMonthPrefix.value)
  )
})

const monthlyTotalVolume = computed(() => {
  return monthlyTransactions.value.reduce((sum, t) => sum + (Number(t.amount) || 0), 0)
})

const monthlyInflows = computed(() => {
  return monthlyTransactions.value
    .filter(t => isIncomeType(t.transaction_type))
    .reduce((sum, t) => sum + (Number(t.amount) || 0), 0)
})

const monthlyOutflows = computed(() => {
  return monthlyTransactions.value
    .filter(t => !isIncomeType(t.transaction_type))
    .reduce((sum, t) => sum + (Number(t.amount) || 0), 0)
})

const monthlyCount = computed(() => monthlyTransactions.value.length)

function isIncomeType(t: string) {
  const incomeTypes = ['deposit', 'income', 'sell', 'transfer_in']
  return incomeTypes.includes(t)
}

function isTradingType(t: string) {
  return t === 'buy' || t === 'sell'
}

function typeLabel(t: string) {
  return transactionTypes.find(x => x.value === t)?.label || t
}

function typeTag(t: string): 'success' | 'warning' | 'danger' | 'info' | 'primary' {
  const map: Record<string, 'success' | 'warning' | 'danger' | 'info' | 'primary'> = {
    deposit: 'success',
    income: 'success',
    buy: 'primary',
    sell: 'warning',
    withdrawal: 'danger',
    loss: 'danger',
    fee: 'info',
    transfer_in: 'success',
    transfer_out: 'warning',
  }
  return map[t] || 'info'
}

function calculateAmount() {
  if (isTradingType(form.transaction_type)) {
    const q = Number(form.quantity) || 0
    const p = Number(form.price_per_unit) || 0
    if (q > 0 && p > 0) {
      form.amount = Number((q * p).toFixed(2))
    }
  }
}

function onAssetChange(assetId: number | null) {
  const asset = assets.value.find(a => a.id === assetId)
  if (asset && asset.currency) {
    form.currency = asset.currency
  }
}

function onTransactionTypeChange(type: string) {
  if (!isTradingType(type)) {
    form.quantity = 0
    form.price_per_unit = 0
  }
}

function onCatChange(val: any) {
  if (Array.isArray(val) && val.length > 0) {
    form.category_id = val[val.length - 1]
  } else if (typeof val === 'number') {
    form.category_id = val
  } else {
    form.category_id = null
  }
}

function resetFilters() {
  filters.asset_id = ''
  filters.category_id = ''
  filters.type = ''
  filters.dateRange = null
  loadData()
}

async function loadAssets() {
  try {
    assets.value = await assetsApi.list()
  } catch (err) {
    ElMessage.error('加载资产列表失败')
  }
}

async function loadCategories() {
  try {
    const res = await categoriesApi.list({ type: 'transaction' })
    allCategories.value = res
  } catch (err) {
    ElMessage.error('加载交易分类失败')
  }
}

async function loadData() {
  loading.value = true
  try {
    const params: any = {}
    if (filters.asset_id) params.asset_id = filters.asset_id
    if (filters.category_id) params.category_id = String(filters.category_id)
    if (filters.type) params.type = filters.type
    if (filters.dateRange?.[0]) params.start_date = filters.dateRange[0]
    if (filters.dateRange?.[1]) params.end_date = filters.dateRange[1]
    const res = await transactionsApi.list(params)
    transactions.value = res
  } catch (err) {
    ElMessage.error('加载交易记录失败')
  } finally {
    loading.value = false
  }
}

function openDialog(txn?: any) {
  editingId.value = txn?.id ?? null
  const cur = txn?.currency ? String(txn.currency) : 'CNY'
  Object.assign(form, txn ? {
    asset_id: Number(txn.asset_id),
    linked_asset_id: Number(txn.linked_asset_id) || null,
    transaction_type: txn.transaction_type,
    amount: Number(txn.amount),
    quantity: Number(txn.quantity) || 0,
    price_per_unit: Number(txn.price_per_unit) || 0,
    transaction_date: txn.transaction_date,
    note: txn.note || '',
    category_id: Number(txn.category_id) || null,
    currency: cur,
    _catPath: [Number(txn.category_id)].filter(Boolean),
  } : {
    asset_id: null,
    linked_asset_id: null,
    transaction_type: 'deposit',
    amount: 0,
    quantity: 0,
    price_per_unit: 0,
    transaction_date: new Date().toISOString().slice(0, 10),
    note: '',
    category_id: null,
    currency: 'CNY',
    _catPath: [],
  })
  dialogVisible.value = true
}

async function handleSubmit() {
  if (!formRef.value) return
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
    if (editingId.value) {
      await transactionsApi.update(editingId.value, form)
    } else {
      await transactionsApi.create(form)
    }
    ElMessage.success('保存成功')
    dialogVisible.value = false
    loadData()
  } catch (err: any) {
    ElMessage.error(err?.message || '保存交易失败')
  } finally {
    saving.value = false
  }
}

async function handleDelete(txn: any) {
  await ElMessageBox.confirm(`确定删除该交易记录吗？`, '提示', { type: 'warning' })
  try {
    await transactionsApi.delete(txn.id)
    ElMessage.success('删除成功')
    loadData()
  } catch (err) {
    ElMessage.error('删除失败')
  }
}

onMounted(() => {
  loadAssets()
  loadCategories()
  loadData()
})
</script>

<style scoped>
.transactions-page {
  padding: 24px;
  background-color: var(--mf-background);
  min-height: 100%;
}

.action-btn {
  border-radius: var(--mf-radius-md);
  padding: 8px 16px;
  font-weight: 500;
}

.summary-cards {
  margin-bottom: 24px;
}

.summary-value {
  font-size: 22px;
  font-weight: 700;
  color: var(--mf-text-main);
}

.font-mono {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.income-text { color: #34d399; text-shadow: 0 0 8px rgba(52,211,153,0.4); }
.expense-text { color: #f87171; text-shadow: 0 0 8px rgba(248,113,113,0.3); }
.text-primary { color: #00d4ff; }

.main-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.filter-panel {
  margin-bottom: 20px;
}

.premium-filters :deep(.el-form-item) {
  margin-bottom: 12px;
  margin-right: 16px;
}

.filter-select {
  width: 170px;
}

.filter-date {
  width: 260px;
}

.table-container {
  overflow-x: auto;
}

.category-pill {
  background: rgba(0, 212, 255, 0.06);
  color: #94a3b8;
  padding: 2px 8px;
  border-radius: 6px;
  font-size: 13px;
  border: 1px solid rgba(0, 212, 255, 0.1);
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 14px;
  color: #e2e8f0;
}

.trade-detail {
  font-size: 13px;
  color: #64748b;
}

.price-tag { color: #00d4ff; }
.times { margin: 0 4px; color: #475569; }
.qty-tag { color: #34d399; }
.muted-text { color: #475569; }

.trading-fields {
  background: var(--mf-surface-muted);
  padding: 12px 12px 0 12px;
  border-radius: var(--mf-radius-md);
  border: 1px solid var(--mf-border);
  margin-bottom: 16px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}
</style>
