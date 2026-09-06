<template>
  <div class="assets-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产</h2>
      </div>
      <div class="header-actions">
        <el-button type="success" plain class="action-btn" :loading="syncingAll" @click="handleSyncAll">
          <el-icon><Refresh /></el-icon> 同步行情
        </el-button>
        <el-button type="primary" class="action-btn" @click="openDialog()">
          <el-icon><Plus /></el-icon> 新增资产
        </el-button>
      </div>
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
        <el-table-column label="名称" min-width="160">
          <template #default="{ row }">
            <div class="asset-name-cell">
              <span class="asset-icon"><Icon :icon="ASSET_TYPE_ICONS[row.asset_type] ?? 'ph:briefcase'" /></span>
              <div class="asset-name-group">
                <span class="asset-name">{{ row.name }}</span>
                <span v-if="row.symbol" class="asset-symbol-sub">
                  <el-tag size="small" effect="plain" class="symbol-tag">{{ row.symbol }}</el-tag>
                  <span v-if="row.last_sync_at" class="last-sync-time">同步于 {{ row.last_sync_at.slice(5, 16) }}</span>
                </span>
              </div>
            </div>
          </template>
        </el-table-column>
        <el-table-column prop="category_name" label="分类" min-width="110" />
        <el-table-column prop="account_no" label="账户编号" min-width="120" />
        <el-table-column label="币种" width="80">
          <template #default="{ row }">
            <el-tag size="small" class="currency-tag" effect="plain">{{ row.currency }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="当前价值" min-width="150" align="right">
          <template #default="{ row }">
            <div style="display: flex; flex-direction: column; align-items: flex-end; justify-content: center;">
              <span class="mono-amount">{{ formatCurrency(row.current_value) }}</span>
              <span v-if="row.currency && row.currency !== 'CNY' && exchangeRates[row.currency]" class="cny-converted-hint">
                ≈ ¥{{ (Number(row.current_value) * (exchangeRates[row.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
              </span>
            </div>
          </template>
        </el-table-column>
        <el-table-column v-if="hasInvestmentAssets" label="份额" min-width="100" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.quantity != null ? Number(row.quantity).toFixed(2) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column v-if="hasInvestmentAssets" label="成本" min-width="110" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.cost_basis != null ? formatCurrency(Number(row.cost_basis)) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column v-if="hasInvestmentAssets" label="净值" min-width="100" align="right">
          <template #default="{ row }">
            <span class="mono-amount">{{ row.net_value != null ? Number(row.net_value).toFixed(4) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="160" align="center">
          <template #default="{ row }">
            <div class="action-buttons">
              <el-tooltip v-if="row.symbol" content="同步最新行情" placement="top">
                <el-button link type="success" size="small" :loading="syncingId === row.id" :icon="Refresh" @click="handleSyncSingle(row)" />
              </el-tooltip>
              <el-tooltip v-if="row.symbol || isInvestmentType(row.asset_type)" content="查看净值走势" placement="top">
                <el-button link type="warning" size="small" :icon="TrendCharts" @click="openHistoryChart(row)" />
              </el-tooltip>
              <el-tooltip content="编辑" placement="top">
                <el-button link type="primary" size="small" :icon="Edit" @click="openDialog(row)" />
              </el-tooltip>
              <el-tooltip content="删除" placement="top">
                <el-button link type="danger" size="small" :icon="Delete" @click="handleDelete(row)" />
              </el-tooltip>
            </div>
          </template>
        </el-table-column>
      </el-table>
      <div class="pagination-bar">
        <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadAssets" @size-change="handleSizeChange" />
      </div>
    </div>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑资产' : '新增资产'" width="520px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="95px" class="premium-form">
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :props="{ checkStrictly: true, value: 'id', label: 'name', lazy: true, lazyLoad(node: any, resolve: any) {
              if (node.level === 0) {
                resolve(categoryStore.allNodes.filter(c => c.type === 'asset' || (!c.type && c.asset_type) && (c.parent_id === null || c.parent_id === 0)) as any)
              } else {
                categoryStore.loadChildren(node.data.id as number).then((children: any) => resolve(children))
              }
            } }"
            placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>

        <template v-if="isInvestment">
          <el-form-item label="标的代码">
            <SymbolSelect v-model="form.symbol" @select="handleSymbolSelect" />
          </el-form-item>
        </template>

        <el-form-item label="资产名称" prop="name">
          <el-input v-model="form.name" placeholder="如：招商银行卡、贵州茅台、易方达精选" />
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
            <el-option label="HKD" value="HKD" />
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

    <!-- 净值走势图对话框 -->
    <el-dialog v-model="historyDialogVisible" title="净值/价格历史走势" width="680px" class="premium-dialog" destroy-on-close>
      <PriceHistoryChart
        v-if="selectedHistoryAsset"
        :asset-id="selectedHistoryAsset.id"
        :asset-name="selectedHistoryAsset.name"
        :currency="selectedHistoryAsset.currency"
      />
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Edit, Delete, Plus, Refresh, TrendCharts } from '@element-plus/icons-vue'
import { assetsApi } from '@/api/assets'
import { summaryApi } from '@/api/summary'
import { marketApi } from '@/api/market'
import { useCategoryStore } from '@/stores/category'
import type { Asset, Category, Summary, MarketSearchItem } from '@/types'
import SummaryCard from '@/components/SummaryCard.vue'
import SymbolSelect from '@/components/SymbolSelect.vue'
import PriceHistoryChart from '@/components/PriceHistoryChart.vue'
import { formatCurrency } from '@/utils/format'
import { Icon } from '@iconify/vue'

const ASSET_TYPE_ICONS: Record<string, string> = {
  bank: 'ph:bank',
  cash: 'ph:cash',
  alipay: 'ph:device-mobile',
  wechat: 'ph:chat-circle',
  credit_card: 'ph:credit-card',
  stock: 'ph:trend-up',
  fund: 'ph:trend-up',
  bond: 'ph:trend-up',
  crypto: 'ph:currency-btc',
  loan: 'ph:arrow-down-left',
  real_estate: 'ph:house',
}

const assets = ref<Asset[]>([])
const categoryTree = ref<Category[]>([])
const categoryStore = useCategoryStore()
const dialogVisible = ref(false)
const historyDialogVisible = ref(false)
const selectedHistoryAsset = ref<Asset | null>(null)
const editingId = ref<number | null>(null)
const saving = ref(false)
const syncingAll = ref(false)
const syncingId = ref<number | null>(null)
const formRef = ref()
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, category_breakdown: [], trend: [] })
const exchangeRates = ref<Record<string, number>>({})

const totalAssets = computed(() => summary.value.total_assets)
const totalLiabilities = computed(() => summary.value.total_liabilities)
const netWorth = computed(() => summary.value.net_worth)

const hasInvestmentAssets = computed(() =>
  assets.value.some(a => isInvestmentType(a.asset_type))
)

function isInvestmentType(type?: string) {
  return ['stock', 'fund', 'bond', 'crypto'].includes(type ?? '')
}

async function loadRates() {
  try {
    const res = await marketApi.getExchangeRates()
    if (res) exchangeRates.value = res
  } catch (err) {
    console.error('[Assets] loadRates failed:', err)
  }
}

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

function openHistoryChart(asset: any) {
  selectedHistoryAsset.value = asset
  historyDialogVisible.value = true
}

async function handleSyncAll() {
  syncingAll.value = true
  try {
    const res = await marketApi.syncAll()
    ElMessage.success(`行情同步完成：成功 ${res.synced_count} 个，失败 ${res.failed_count} 个`)
    await Promise.all([loadAssets(), loadSummary()])
  } catch (err: any) {
    ElMessage.error(err?.message || '同步行情失败')
  } finally {
    syncingAll.value = false
  }
}

async function handleSyncSingle(asset: any) {
  syncingId.value = asset.id
  try {
    const res = await marketApi.syncSingle(asset.id)
    ElMessage.success(`标的 ${res.name || asset.name} 最新净值：${(res.current_price || (res as any).net_value || 0).toFixed(4)}`)
    await Promise.all([loadAssets(), loadSummary()])
  } catch (err: any) {
    ElMessage.error(err?.message || '同步行情失败')
  } finally {
    syncingId.value = null
  }
}

function handleSymbolSelect(item: MarketSearchItem) {
  form.symbol = item.symbol
  form.quote_source = item.source
  if (!form.name || form.name.trim() === '') {
    form.name = item.name
  }
  if (item.currency) {
    form.currency = item.currency
  }
  /* Fetch latest price */
  marketApi.getQuote(item.symbol, item.source).then(q => {
    if (q && q.current_price > 0) {
      form.net_value = q.current_price
    }
  }).catch(() => {})
}

function openDialog(asset?: any) {
  editingId.value = asset?.id ?? null
  const isInv = isInvestmentType(asset?.asset_type)
  Object.assign(form, asset ? {
    name: asset.name, category_id: asset.category_id, account_no: asset.account_no,
    symbol: asset.symbol ?? '', quote_source: asset.quote_source ?? '',
    current_value: asset.current_value, currency: asset.currency, note: asset.note,
    quantity: asset.quantity ?? 0, net_value: asset.net_value ?? 0,
    cost_basis: asset.cost_basis ?? 0,
    _catPath: [asset.category_id],
    _isInvestment: isInv,
  } : { name: '', category_id: null, account_no: '', symbol: '', quote_source: '', current_value: 0, currency: 'CNY', note: '', quantity: 0, net_value: 0, cost_basis: 0, _catPath: [], _isInvestment: false })
  dialogVisible.value = true
}

const form = reactive({
  name: '',
  category_id: null as number | null,
  account_no: '',
  symbol: '',
  quote_source: '',
  current_value: 0,
  currency: 'CNY',
  note: '',
  quantity: 0,
  net_value: 0,
  cost_basis: 0,
  _catPath: [] as number[],
  _isInvestment: false as boolean
})

const isInvestment = computed(() => form._isInvestment)
const rules = {
  name: [{ required: true, message: '请输入资产名称' }],
  category_id: [{ required: true, message: '请选择分类' }]
}

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
  form._isInvestment = isInvestmentType(cat?.asset_type)
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    saving.value = true
    try {
      const data: any = {
        name: form.name,
        category_id: form.category_id,
        account_no: form.account_no,
        currency: form.currency,
        note: form.note,
        symbol: form.symbol,
        quote_source: form.quote_source,
      }
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
      loadSummary()
    } finally { saving.value = false }
  })
}

async function handleDelete(asset: any) {
  await ElMessageBox.confirm(`确定删除资产「${asset.name}」吗？`, '提示', { type: 'warning' })
  await assetsApi.delete(asset.id)
  ElMessage.success('删除成功')
  loadAssets()
  loadSummary()
}

onMounted(async () => {
  try {
    await Promise.all([loadAssets(), loadSummary(), loadCategories(), loadRates()])
  } catch (err) {
    console.error('[Assets] onMounted failed:', err)
  }
})
</script>

<style scoped>
.assets-page {
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
  padding: 10px 16px;
  transition: all 0.2s ease;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 8px;
}

.title-accent {
  width: 4px;
  height: 18px;
  background-color: var(--el-color-primary);
  border-radius: 2px;
}

.header-actions {
  display: flex;
  gap: 8px;
}

.asset-name-cell {
  display: flex;
  align-items: center;
  gap: 10px;
}

.asset-icon {
  font-size: 20px;
  color: var(--el-color-primary);
  display: flex;
  align-items: center;
}

.asset-name-group {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.asset-name {
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.asset-symbol-sub {
  display: flex;
  align-items: center;
  gap: 6px;
}

.symbol-tag {
  font-family: monospace;
  font-size: 11px;
}

.last-sync-time {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.mono-amount {
  font-family: monospace;
  font-weight: 500;
}

.cny-converted-hint {
  font-size: 11px;
  color: var(--mf-text-muted, #94a3b8);
  font-variant-numeric: tabular-nums;
  margin-top: 2px;
}

.action-buttons {
  display: flex;
  justify-content: center;
  gap: 4px;
}

.investment-hint {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: -8px;
  margin-bottom: 14px;
  margin-left: 95px;
}

.table-container {
  flex: 1;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 12px;
}
</style>
