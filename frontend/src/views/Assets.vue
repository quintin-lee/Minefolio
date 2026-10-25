<template>
  <div class="assets-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产</h2>
      </div>
      <div class="header-actions">
        <el-button plain class="action-btn" @click="openAccountDialog()">
          <el-icon><Wallet /></el-icon> 账户
        </el-button>
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

    <!-- 资产列表（按账户分组） -->
    <div class="table-container">
      <div v-if="groupedAccounts.length === 0 && !loading" class="empty-state">暂无资产数据</div>
      <div v-for="group in groupedAccounts" :key="group.key" class="account-group">
        <div class="account-group-header" @click="toggleGroup(group.key)">
          <div class="group-title">
            <span class="account-avatar"><Icon :icon="group.icon || 'ph:wallet'" /></span>
            <div class="group-name-wrap">
              <span class="group-name">{{ group.label }}</span>
              <span v-if="group.institution" class="institution-tag">{{ group.institution }}</span>
              <el-tag v-if="group.archived" size="small" type="info">已归档</el-tag>
            </div>
          </div>
          <div class="group-meta">
            <span class="group-count">{{ group.assets.length }} 项资产</span>
            <span v-if="group.subtotalCny !== 0" class="group-subtotal mono-amount">≈ ¥{{ group.subtotalCny.toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}</span>
            <el-tooltip v-if="group.editable" content="编辑账户" placement="top">
              <el-button class="group-edit" type="primary" link :icon="Edit" @click.stop="openAccountDialog(group.account)" />
            </el-tooltip>
            <el-icon class="group-chevron">
              <ArrowDown v-if="collapsedGroups.has(group.key)" />
              <ArrowUp v-else />
            </el-icon>
          </div>
        </div>
        <div v-if="!collapsedGroups.has(group.key)" class="account-group-body">
          <div v-for="a in group.assets" :key="a.id" class="asset-row">
            <div class="asset-name-cell">
              <span class="asset-icon"><Icon :icon="ASSET_TYPE_ICONS[a.asset_type ?? ''] ?? 'ph:briefcase'" /></span>
              <div class="asset-name-group">
                <span class="asset-name">{{ a.name }}</span>
                <span v-if="a.symbol" class="asset-symbol-sub">
                  <el-tag size="small" effect="plain" class="symbol-tag">{{ a.symbol }}</el-tag>
                  <span v-if="a.last_sync_at" class="last-sync-time">同步于 {{ a.last_sync_at.slice(5, 16) }}</span>
                </span>
                <span v-if="hasInvestmentAssets && isInvestmentType(a.asset_type)" class="asset-sub-meta">
                  {{ Number(a.quantity ?? 0).toFixed(2) }} 份 · 成本 {{ formatCurrency(Number(a.cost_basis ?? 0), a.currency) }} · 净值 {{ Number(a.net_value ?? 0).toFixed(4) }}
                </span>
              </div>
            </div>
            <span class="asset-cat">{{ a.category_name }}</span>
            <div class="value-col">
              <span class="mono-amount">{{ formatCurrency(a.current_value, a.currency) }}</span>
              <span v-if="a.currency && a.currency !== 'CNY' && exchangeRates[a.currency]" class="cny-converted-hint">
                ≈ ¥{{ (Number(a.current_value) * (exchangeRates[a.currency] || 1)).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 }) }}
              </span>
            </div>
            <div class="row-actions">
              <el-tooltip v-if="a.symbol" content="同步最新行情" placement="top">
                <el-button link type="success" size="small" :loading="syncingId === a.id" :icon="Refresh" @click="handleSyncSingle(a)" />
              </el-tooltip>
              <el-tooltip v-if="a.symbol || isInvestmentType(a.asset_type)" content="查看净值走势" placement="top">
                <el-button link type="warning" size="small" :icon="TrendCharts" @click="openHistoryChart(a)" />
              </el-tooltip>
              <el-tooltip content="编辑" placement="top">
                <el-button link type="primary" size="small" :icon="Edit" @click="openDialog(a)" />
              </el-tooltip>
              <el-tooltip content="删除" placement="top">
                <el-button link type="danger" size="small" :icon="Delete" @click="handleDelete(a)" />
              </el-tooltip>
            </div>
          </div>
          <div v-if="group.assets.length === 0" class="group-empty">暂无资产</div>
        </div>
      </div>
      <el-button v-if="groupedAccounts.length > 0" class="add-account-cta" plain @click="openAccountDialog()">
        <el-icon><Wallet /></el-icon> 新建账户
      </el-button>
    </div>

    <!-- 新增/编辑对话框 -->
    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑资产' : '新增资产'" width="520px" class="premium-dialog" :show-close="false">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="95px" class="premium-form">
        <el-form-item label="分类" prop="category_id">
          <el-cascader v-model="form._catPath" :props="{ checkStrictly: true, value: 'id', label: 'name', lazy: true, lazyLoad(node: any, resolve: any) {
              if (node.level === 0) {
                // 顶层只展示根级资产分类：必须同时满足“是资产分类”与“无父级”，
                // 否则已懒加载过的子级会被 `||` 短路提升到根层重复展示
                resolve(categoryStore.allNodes.filter(c => ((c.type === 'asset' || (!c.type && c.asset_type)) && (c.parent_id === null || c.parent_id === 0))) as any)
              } else {
                categoryStore.loadChildren(node.data.id as number).then((children: any) => resolve(children))
              }
            } }"
            placeholder="选择分类" style="width: 100%" @change="onCatChange" />
        </el-form-item>

        <el-form-item label="归属账户">
          <el-select v-model="form.account_id" style="width: 100%" clearable placeholder="默认账户">
            <el-option v-for="acc in accounts" :key="acc.id" :label="`${acc.name}${acc.account_no ? '（' + acc.account_no + '）' : ''}`" :value="acc.id" />
          </el-select>
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

    <!-- 账户管理对话框 -->
    <AccountDialog v-model:visible="accountDialogVisible" :account="editingAccount" @saved="handleAccountSaved" />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Edit, Delete, Plus, Refresh, TrendCharts, Wallet, ArrowDown, ArrowUp } from '@element-plus/icons-vue'
import { assetsApi } from '@/api/assets'
import { accountsApi } from '@/api/accounts'
import { summaryApi } from '@/api/summary'
import { marketApi } from '@/api/market'
import { useCategoryStore } from '@/stores/category'
import type { Asset, Category, Summary, MarketSearchItem, Account } from '@/types'
import SummaryCard from '@/components/SummaryCard.vue'
import SymbolSelect from '@/components/SymbolSelect.vue'
import PriceHistoryChart from '@/components/PriceHistoryChart.vue'
import AccountDialog from '@/components/AccountDialog.vue'
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
const loading = ref(false)
const saving = ref(false)
const syncingAll = ref(false)
const syncingId = ref<number | null>(null)
const formRef = ref()
const summary = ref<Summary>({ total_assets: 0, total_liabilities: 0, net_worth: 0, category_breakdown: [], trend: [] })

/* 账户维度 */
const accounts = ref<Account[]>([])
const accountDialogVisible = ref(false)
const editingAccount = ref<Account | null>(null)
const collapsedGroups = ref<Set<string>>(new Set())
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
  try {
    summary.value = await summaryApi.get()
  } catch (err) {
    console.error('[Assets] loadSummary failed:', err)
  }
}

async function loadAssets() {
  loading.value = true
  try {
    const res = await assetsApi.list({ page_size: 500 })
    assets.value = res.list
  } catch (err) {
    console.error('[Assets] loadAssets failed:', err)
  } finally {
    loading.value = false
  }
}

async function loadAccounts() {
  try {
    accounts.value = await accountsApi.list({ include_archived: 1 })
  } catch (err) {
    console.error('[Assets] loadAccounts failed:', err)
  }
}

const groupedAccounts = computed(() => {
  const institutionLabels: Record<string, string> = {
    bank_card: '银行卡',
    platform: '平台账户',
    broker: '券商',
    fund_platform: '基金平台',
    cash_wallet: '现金钱包',
    other: '其他',
  }
  type Group = { key: string; label: string; icon: string; institution: string; archived: boolean; account: Account | null; assets: Asset[]; subtotalCny: number; editable: boolean }
  const byKey = new Map<string, Group>()
  for (const acc of accounts.value) {
    byKey.set(`acct-${acc.id}`, {
      key: `acct-${acc.id}`,
      label: acc.name,
      icon: acc.icon || 'ph:wallet',
      institution: institutionLabels[acc.institution] || acc.institution,
      archived: !!acc.archived,
      account: acc,
      assets: [],
      subtotalCny: 0,
      editable: true,
    })
  }
  for (const a of assets.value) {
    const known = a.account_id && a.account_id > 0 ? byKey.get(`acct-${a.account_id}`) : undefined
    const key = known ? known.key : 'ungrouped'
    let g = byKey.get(key)
    if (!g) {
      g = {
        key: 'ungrouped',
        label: '未分组',
        icon: 'ph:wallet',
        institution: '',
        archived: false,
        account: null,
        assets: [],
        subtotalCny: 0,
        editable: false,
      }
      byKey.set(key, g)
    }
    g.assets.push(a)
    const rate = a.currency && a.currency !== 'CNY' ? exchangeRates.value[a.currency] || 1 : 1
    g.subtotalCny += Number(a.current_value ?? 0) * rate
  }
  const groups = Array.from(byKey.values())
  groups.sort((x, y) => {
    if (x.key === 'ungrouped') return 1
    if (y.key === 'ungrouped') return -1
    const xa = x.account
    const ya = y.account
    if (!xa && !ya) return 0
    if (!xa) return 1
    if (!ya) return -1
    return (xa.sort_order - ya.sort_order) || (xa.id - ya.id)
  })
  return groups
})

function toggleGroup(key: string) {
  if (collapsedGroups.value.has(key)) {
    collapsedGroups.value.delete(key)
  } else {
    collapsedGroups.value.add(key)
  }
}

function openAccountDialog(acc?: Account | null) {
  editingAccount.value = acc ?? null
  accountDialogVisible.value = true
}

async function handleAccountSaved() {
  await loadAccounts()
  await loadAssets()
  await loadSummary()
}

async function loadCategories() {
  try {
    await categoryStore.loadCategories()
    categoryTree.value = categoryStore.assetCategories
  } catch (err) {
    console.error('[Assets] loadCategories failed:', err)
  }
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
    account_id: asset.account_id ?? null,
    _catPath: [asset.category_id],
    _isInvestment: isInv,
  } : { name: '', category_id: null, account_no: '', symbol: '', quote_source: '', current_value: 0, currency: 'CNY', note: '', quantity: 0, net_value: 0, cost_basis: 0, account_id: null, _catPath: [], _isInvestment: false })
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
  account_id: null as number | null,
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
        account_id: form.account_id ?? 0,
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
    await loadAccounts()
    await Promise.allSettled([loadAssets(), loadSummary(), loadCategories(), loadRates()])
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
  flex-shrink: 0;
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

.asset-summary {
  flex-shrink: 0;
}

.table-container {
  overflow: hidden;
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  box-shadow: var(--mf-shadow-sm);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
}

.table-container :deep(.el-table) {
  flex: 1;
  min-height: 0;
  height: 100%;
}

.table-container :deep(.el-table__body-wrapper) {
  overflow-y: auto;
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
  flex-shrink: 0;
}
.account-group {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: var(--mf-radius-md, 8px);
  overflow: hidden;
  margin-bottom: 12px;
}

.account-group-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  cursor: pointer;
  user-select: none;
  background: var(--el-fill-color-lighter);
}

.group-title {
  display: flex;
  align-items: center;
  gap: 10px;
}

.account-avatar {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 8px;
  background: var(--el-color-primary-light-8);
  color: var(--el-color-primary);
  font-size: 18px;
}

.group-name-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.group-name {
  font-weight: 600;
  font-size: 14px;
  color: var(--el-text-color-primary);
}

.institution-tag {
  font-size: 11px;
  padding: 1px 8px;
  border-radius: 999px;
  background: rgba(0, 212, 255, 0.08);
  color: var(--el-text-color-secondary);
  border: 1px solid rgba(0, 212, 255, 0.15);
}

.group-meta {
  display: flex;
  align-items: center;
  gap: 14px;
}

.group-count {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.group-subtotal {
  font-size: 14px;
  color: var(--el-color-primary);
}

.group-chevron {
  color: var(--el-text-color-secondary);
}

.account-group-body {
  padding: 8px 12px;
}

.asset-row {
  display: grid;
  grid-template-columns: minmax(220px, 1.4fr) 100px minmax(160px, 1fr) 150px;
  align-items: center;
  gap: 12px;
  padding: 10px 8px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.asset-row:last-child {
  border-bottom: none;
}

.asset-cat {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.asset-sub-meta {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.row-actions {
  display: flex;
  gap: 4px;
  justify-content: flex-end;
}

.value-col {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
}

.group-empty {
  padding: 20px;
  text-align: center;
  color: var(--el-text-color-secondary);
  font-size: 13px;
}

.add-account-cta {
  width: 100%;
  margin-top: 4px;
  border-style: dashed;
}

.empty-state {
  text-align: center;
  color: var(--el-text-color-secondary);
  padding: 48px 0;
}
</style>
