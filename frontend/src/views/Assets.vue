<template>
  <div class="assets-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog()">
        <el-icon><Plus /></el-icon> 新增资产
      </el-button>
    </div>

    <!-- 总资产概览 -->
    <el-row :gutter="24" class="asset-summary">
      <el-col :span="8">
        <SummaryCard label="总资产" :value="formatCurrency(totalAssets)" type="income" />
      </el-col>
      <el-col :span="8">
        <SummaryCard label="总负债" :value="formatCurrency(totalLiabilities)" type="expense" />
      </el-col>
      <el-col :span="8">
        <SummaryCard label="净资产" :value="formatCurrency(netWorth)" type="highlight" />
      </el-col>
    </el-row>

    <!-- 资产列表 -->
    <div class="table-container">
      <el-table :data="assets" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
        <el-table-column label="名称" min-width="150">
          <template #default="{ row }">
            <div class="asset-name-cell">
              <span class="asset-icon">{{ {'bank': '🏦', 'cash': '💵', 'alipay': '📱', 'wechat': '💬', 'credit_card': '💳', 'stock': '📈', 'fund': '📊', 'crypto': '🪙', 'loan': '💸', 'real_estate': '🏠'}[row.asset_type as string] || '💼' }}</span>
              <span class="asset-name">{{ row.name }}</span>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="category_name" label="分类" min-width="120" />
        <el-table-column prop="account_no" label="账户编号" min-width="140" />
        <el-table-column label="币种" width="100">
          <template #default="{ row }">
            <el-tag size="small" class="currency-tag" effect="plain">{{ row.currency }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="当前价值" min-width="160" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ formatCurrency(row.current_value) }}</span>
          </template>
        </el-table-column>
        <el-table-column v-if="assetTypeShow('stock','fund','bond','crypto')" label="份额" min-width="100" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.quantity != null ? Number(row.quantity).toFixed(2) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column v-if="assetTypeShow('stock','fund','bond','crypto')" label="成本" min-width="120" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.cost_basis != null ? formatCurrency(Number(row.cost_basis)) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column v-if="assetTypeShow('stock','fund','bond','crypto')" label="净值" min-width="100" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.net_value != null ? Number(row.net_value).toFixed(4) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="140" align="center">
          <template #default="{ row }">
            <div class="action-buttons">
              <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
              <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>
      <div class="pagination-bar">
        <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadAssets" @size-change="handleSizeChange" />
      </div>
    </div>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑资产' : '新增资产'" width="480px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-form-item label="资产名称" prop="name">
          <el-input v-model="form.name" placeholder="如：招商银行卡、茅台股票" />
        </el-form-item>
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :props="{ checkStrictly: true, value: 'id', label: 'name', lazy: true, lazyLoad(node, resolve) {
              if (node.level === 0) {
                resolve(categoryStore.allNodes.filter(c => c.type === 'asset' || (!c.type && c.asset_type) && (c.parent_id === null || c.parent_id === 0)) as any)
              } else {
                categoryStore.loadChildren(node.data.id as number).then((children: any) => resolve(children))
              }
            } }"
            placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>
        <el-form-item label="账户编号">
          <el-input v-model="form.account_no" placeholder="可选" />
        </el-form-item>
        <el-form-item label="当前价值" prop="current_value" v-if="!isInvestment">
          <el-input-number v-model="form.current_value" :precision="2" :min="0" style="width: 100%" :controls="false" />
        </el-form-item>
        <template v-if="isInvestment">
          <el-form-item label="持有份额">
            <el-input-number v-model="form.quantity" :precision="4" :min="0" style="width: 100%" :controls="false" :disabled="editingId !== null" />
          </el-form-item>
          <el-form-item label="单位净值">
            <el-input-number v-model="form.net_value" :precision="4" :min="0" style="width: 100%" :controls="false" />
          </el-form-item>
          <el-form-item label="持仓成本">
            <el-input-number v-model="form.cost_basis" :precision="2" :min="0" style="width: 100%" :controls="false" placeholder="留空默认 = 份额 × 净值" />
          </el-form-item>
          <div class="investment-hint">{{ editingId ? '净值更新后将重新计算市值' : '市值将按 份额 × 净值 自动计算；成本留空则等同市值' }}</div>
        </template>
        <el-form-item label="币种">
          <el-select v-model="form.currency" style="width: 100%">
            <el-option label="CNY" value="CNY" />
            <el-option label="USD" value="USD" />
            <el-option label="EUR" value="EUR" />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" type="textarea" :rows="3" placeholder="添加备注..." />
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button class="save-btn" type="primary" :loading="saving" @click="handleSubmit">保存资产</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { assetsApi } from '@/api/assets'
import { summaryApi } from '@/api/summary'
import { useCategoryStore } from '@/stores/category'
import type { Asset, Category, Summary } from '@/types'
import SummaryCard from '@/components/SummaryCard.vue'
import { formatCurrency } from '@/utils/format'

const assets = ref<Asset[]>([])
const categoryTree = ref<Category[]>([])
const categoryStore = useCategoryStore()
const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, breakdown: [], trend: [] })

const totalAssets = computed(() => summary.value.total_assets)
const totalLiabilities = computed(() => summary.value.total_liabilities)
const netWorth = computed(() => summary.value.net_worth)

async function loadSummary() {
  summary.value = await summaryApi.get()
}


async function loadAssets() {
  const res = await assetsApi.list({ page: page.value, page_size: pageSize.value })
  assets.value = res.list
  total.value = res.total
}

function handleSizeChange() {
  page.value = 1
  loadAssets()
}

async function loadCategories() {
  await categoryStore.loadCategories()
  categoryTree.value = categoryStore.assetCategories
}

function openDialog(asset?: any) {
  editingId.value = asset?.id ?? null
  const isInv = ['stock','fund','bond','crypto'].includes(asset?.asset_type ?? '')
  Object.assign(form, asset ? {
    name: asset.name, category_id: asset.category_id, account_no: asset.account_no,
    current_value: asset.current_value, currency: asset.currency, note: asset.note,
    quantity: asset.quantity ?? 0, net_value: asset.net_value ?? 0,
    cost_basis: asset.cost_basis ?? 0,
    _catPath: [asset.category_id],
    _isInvestment: isInv,
  } : { name: '', category_id: null, account_no: '', current_value: 0, currency: 'CNY', note: '', quantity: 0, net_value: 0, cost_basis: 0, _catPath: [], _isInvestment: false })
  dialogVisible.value = true
}

const form = reactive({ name: '', category_id: null as number | null, account_no: '', current_value: 0, currency: 'CNY', note: '', quantity: 0, net_value: 0, cost_basis: 0, _catPath: [] as number[], _isInvestment: false as boolean })
const isInvestment = computed(() => form._isInvestment)
function assetTypeShow(...types: string[]) {
  return (a: Asset) => types.includes(a.asset_type ?? '')
}
const rules = { name: [{ required: true, message: '请输入资产名称' }], category_id: [{ required: true, message: '请选择分类' }] }

function findCategory(nodes: Category[], id: number): Category | null {
  for (const node of nodes) {
    if (node.id === id) return node
    if (node.children?.length) {
      const found = findCategory(node.children, id)
      if (found) return found
    }
  }
  return null
}

function onCatChange(val: any) {
  const arr = (val as number[]) ?? []
  const catId = arr[arr.length - 1] ?? null
  form.category_id = catId
  const cat = catId != null ? findCategory(categoryTree.value, catId) : null
  form._isInvestment = ['stock','fund','bond','crypto'].includes(cat?.asset_type ?? '')
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data: any = { name: form.name, category_id: form.category_id, account_no: form.account_no, currency: form.currency, note: form.note }
      if (form._isInvestment) {
        data.quantity = form.quantity
        data.net_value = form.net_value
        data.cost_basis = form.cost_basis
      } else {
        data.current_value = form.current_value
      }
      if (editingId.value) {
        await assetsApi.update(editingId.value, data)
        ElMessage.success('更新成功')
      } else {
        await assetsApi.create(data)
        ElMessage.success('创建成功')
      }
      dialogVisible.value = false
      loadAssets()
    } finally { saving.value = false }
  })
}

async function handleDelete(asset: any) {
  await ElMessageBox.confirm(`确定删除资产「${asset.name}」吗？`, '提示', { type: 'warning' })
  await assetsApi.delete(asset.id)
  ElMessage.success('删除成功')
  loadAssets()
}

onMounted(async () => {
  try {
    await Promise.all([loadAssets(), loadSummary(), loadCategories()])
  } catch (err) {
    console.error('[Assets] onMounted failed:', err)
  }
})
</script>

<style scoped>
.assets-page {
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
  box-shadow: 0 4px 6px -1px rgba(59, 130, 246, 0.2);
  transition: all 0.2s ease;
}

.action-btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 8px -1px rgba(59, 130, 246, 0.3);
}

.asset-summary {
  margin-bottom: 24px;
}

.summary-value {
  font-size: 26px;
  font-weight: 700;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  letter-spacing: -0.5px;
}

.income-text { color: #10b981; }
.expense-text { color: #ef4444; }

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

.asset-name-cell {
  display: flex;
  align-items: center;
  gap: 12px;
}

.asset-icon {
  font-size: 20px;
  background: var(--mf-surface-muted);
  padding: 8px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  line-height: 1;
}

.asset-name {
  font-weight: 500;
  color: #e2e8f0;
}

.currency-tag {
  border-radius: 6px;
  font-weight: 600;
  letter-spacing: 0.5px;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  color: #e2e8f0;
  font-size: 15px;
}

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: center;
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

.investment-hint {
  font-size: 12px;
  color: #64748b;
  margin-top: -12px;
  margin-bottom: 24px;
  padding-left: 90px;
}

.premium-form :deep(.el-input__wrapper) {
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.05);
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
</style>
