<template>
  <div class="transfer-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>资产转账</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog">
        <el-icon><Plus /></el-icon> 发起划转
      </el-button>
    </div>

    <!-- 顶部汇总统计卡片 -->
    <el-row :gutter="16" class="summary-cards">
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">本月划转总额</div>
          <div class="summary-value transfer-text">{{ formatCurrency(monthlyTotalAmount) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card">
          <div class="summary-label">本月划转笔数</div>
          <div class="summary-value font-mono">{{ monthlyCount }} 笔</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="summary-card highlight-card">
          <div class="summary-label">参与划转资产数</div>
          <div class="summary-value font-mono">{{ activeAssetCount }} 个账户</div>
        </div>
      </el-col>
    </el-row>

    <!-- 筛选面板与历史记录表格 -->
    <div class="main-panel">
      <div class="filter-panel">
        <el-form :inline="true" class="premium-filters">
          <el-form-item label="关联资产">
            <el-select v-model="filters.asset_id" placeholder="全部资产" clearable class="filter-select">
              <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="String(a.id)" />
            </el-select>
          </el-form-item>
          <el-form-item label="日期范围">
            <el-date-picker
              v-model="filters.dateRange"
              type="daterange"
              range-separator="至"
              start-placeholder="开始日期"
              end-placeholder="结束日期"
              value-format="YYYY-MM-DD"
              class="filter-date-range"
            />
          </el-form-item>
          <el-form-item class="filter-actions">
            <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
            <el-button @click="resetFilters">重置</el-button>
          </el-form-item>
        </el-form>
      </div>

      <div class="table-container">
        <el-table
          v-loading="loading"
          :data="transfers"
          class="premium-table"
          row-class-name="premium-row"
          header-cell-class-name="premium-header"
        >
          <el-table-column prop="transaction_date" label="划转日期" width="130" />
          <el-table-column prop="asset_name" label="账户资产" min-width="140" />
          <el-table-column prop="transaction_type" label="方向" width="100">
            <template #default="{ row }">
              <el-tag
                :type="row.transaction_type === 'transfer_in' ? 'success' : 'warning'"
                effect="light"
                class="type-badge"
                round
              >
                {{ row.transaction_type === 'transfer_in' ? '转入' : '转出' }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="amount" label="划转金额" width="150" align="right">
            <template #default="{ row }">
              <span :class="['mono-amount', row.transaction_type === 'transfer_in' ? 'income-text' : 'expense-text']">
                {{ row.transaction_type === 'transfer_in' ? '+' : '-' }}{{ formatCurrency(row.amount) }}
              </span>
            </template>
          </el-table-column>
          <el-table-column prop="currency" label="币种" width="90" align="center">
            <template #default="{ row }">
              <el-tag size="small" type="info" effect="plain">{{ row.currency || 'CNY' }}</el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="note" label="备注说明" min-width="200">
            <template #default="{ row }">
              <span>{{ row.note || '-' }}</span>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>

    <!-- 发起转账对话框 Modal -->
    <el-dialog
      v-model="dialogVisible"
      title="发起资产划转"
      width="560px"
      class="premium-dialog"
      destroy-on-close
    >
      <div class="visual-flow">
        <div class="asset-box">
          <div class="asset-label">转出账户</div>
          <div class="asset-name" v-if="form.from_asset_id">
            {{ getAssetName(form.from_asset_id) }}
          </div>
          <div class="asset-placeholder" v-else>未选择</div>
        </div>

        <div class="flow-arrow">
          <el-icon class="arrow-icon"><Right /></el-icon>
          <div class="flow-amount" v-if="form.amount > 0">
            {{ formatCurrency(form.amount) }}
          </div>
        </div>

        <div class="asset-box">
          <div class="asset-label">转入账户</div>
          <div class="asset-name" v-if="form.to_asset_id">
            {{ getAssetName(form.to_asset_id) }}
          </div>
          <div class="asset-placeholder" v-else>未选择</div>
        </div>
      </div>

      <el-form
        ref="formRef"
        :model="form"
        :rules="rules"
        label-width="90px"
        class="premium-form"
        label-position="top"
      >
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="转出资产" prop="from_asset_id">
              <el-select v-model="form.from_asset_id" placeholder="选择转出账户" style="width: 100%">
                <el-option
                  v-for="a in assets"
                  :key="a.id"
                  :label="`${a.name} (${formatCurrency(a.current_value)})`"
                  :value="a.id"
                  :disabled="a.id === form.to_asset_id"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="转入资产" prop="to_asset_id">
              <el-select v-model="form.to_asset_id" placeholder="选择转入账户" style="width: 100%">
                <el-option
                  v-for="a in assets"
                  :key="a.id"
                  :label="`${a.name} (${formatCurrency(a.current_value)})`"
                  :value="a.id"
                  :disabled="a.id === form.from_asset_id"
                />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="划转金额" prop="amount">
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
            <el-form-item label="划转日期" prop="transfer_date">
              <el-date-picker
                v-model="form.transfer_date"
                type="date"
                value-format="YYYY-MM-DD"
                style="width: 100%"
                placeholder="选择日期"
              />
            </el-form-item>
          </el-col>
        </el-row>

        <el-form-item label="备注说明">
          <el-input
            v-model="form.note"
            placeholder="填写划转原因或备注说明..."
            type="textarea"
            :rows="2"
          />
        </el-form-item>
      </el-form>

      <div class="transfer-info">
        <h4><el-icon><InfoFilled /></el-icon> 划转说明规则</h4>
        <ul>
          <li>转账会在两个资产间同步记录对应的“转出”与“转入”变动</li>
          <li>划转仅改变内部资产结构分布，不计入个人日常损益</li>
          <li>确认划转后系统将自动更新涉及账户的实时可用余额</li>
        </ul>
      </div>

      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" class="save-btn" :loading="saving" @click="handleSubmit">
            确认划转
          </el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Plus, Right, InfoFilled } from '@element-plus/icons-vue'
import { assetsApi } from '@/api/assets'
import { transactionsApi } from '@/api/transactions'
import { transfersApi } from '@/api/transfers'
import { formatCurrency } from '@/utils/format'
import type { Asset, Transaction } from '@/types'

const loading = ref(false)
const saving = ref(false)
const dialogVisible = ref(false)
const formRef = ref()

const assets = ref<Asset[]>([])
const transfers = ref<Transaction[]>([])

const filters = reactive({
  asset_id: '',
  dateRange: null as string[] | null,
})

const form = reactive({
  from_asset_id: null as number | null,
  to_asset_id: null as number | null,
  amount: 0,
  transfer_date: new Date().toISOString().slice(0, 10),
  note: '',
})

const rules = {
  from_asset_id: [{ required: true, message: '请选择转出账户' }],
  to_asset_id: [{ required: true, message: '请选择转入账户' }],
  amount: [
    { required: true, message: '请输入划转金额' },
    { type: 'number', min: 0.01, message: '划转金额必须大于零' },
  ],
  transfer_date: [{ required: true, message: '请选择划转日期' }],
}

const currentMonthPrefix = computed(() => new Date().toISOString().slice(0, 7))

const monthlyTransfers = computed(() => {
  return transfers.value.filter(t => t.transaction_date && t.transaction_date.startsWith(currentMonthPrefix.value))
})

const monthlyTotalAmount = computed(() => {
  return monthlyTransfers.value
    .filter(t => t.transaction_type === 'transfer_out')
    .reduce((sum, t) => sum + (Number(t.amount) || 0), 0)
})

const monthlyCount = computed(() => {
  return monthlyTransfers.value.filter(t => t.transaction_type === 'transfer_out').length
})

const activeAssetCount = computed(() => {
  const set = new Set(transfers.value.map(t => t.asset_id))
  return set.size
})

function getAssetName(id: number | null) {
  if (!id) return ''
  const found = assets.value.find(a => a.id === id)
  return found ? found.name : ''
}

function openDialog() {
  Object.assign(form, {
    from_asset_id: null,
    to_asset_id: null,
    amount: 0,
    transfer_date: new Date().toISOString().slice(0, 10),
    note: '',
  })
  dialogVisible.value = true
}

function resetFilters() {
  filters.asset_id = ''
  filters.dateRange = null
  loadData()
}

async function loadAssets() {
  try {
    assets.value = await assetsApi.list()
  } catch (err: any) {
    ElMessage.error('加载资产列表失败')
  }
}

async function loadData() {
  loading.value = true
  try {
    const params: any = {}
    if (filters.asset_id) params.asset_id = filters.asset_id
    if (filters.dateRange && filters.dateRange.length === 2) {
      params.start_date = filters.dateRange[0]
      params.end_date = filters.dateRange[1]
    }
    // Fetch transactions with transfer types
    const list = await transactionsApi.list(params)
    transfers.value = list.filter(
      (t: any) => t.transaction_type === 'transfer_in' || t.transaction_type === 'transfer_out'
    )
  } catch (err: any) {
    ElMessage.error('加载转账记录失败')
  } finally {
    loading.value = false
  }
}

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    if (form.from_asset_id === form.to_asset_id) {
      ElMessage.warning('转出和转入资产不能相同')
      return
    }
    saving.value = true
    try {
      await transfersApi.create({
        from_asset_id: form.from_asset_id!,
        to_asset_id: form.to_asset_id!,
        amount: form.amount,
        transfer_date: form.transfer_date,
        note: form.note,
      })
      ElMessage.success('转账完成')
      dialogVisible.value = false
      loadData()
      loadAssets()
    } catch (err: any) {
      ElMessage.error(err?.message || '转账操作失败')
    } finally {
      saving.value = false
    }
  })
}

onMounted(() => {
  loadAssets()
  loadData()
})
</script>

<style scoped>
.transfer-page {
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
  padding: 8px 16px;
  font-weight: 500;
}

.summary-cards {
  margin-bottom: 24px;
}

.summary-card {
  background: #ffffff;
  border-radius: 14px;
  padding: 20px 24px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
  border: 1px solid #f1f5f9;
}

.highlight-card {
  background: linear-gradient(135deg, #eff6ff 0%, #dbeafe 100%);
  border: 1px solid #bfdbfe;
}

.summary-label {
  font-size: 13px;
  color: #64748b;
  margin-bottom: 8px;
}

.summary-value {
  font-size: 24px;
  font-weight: 700;
  color: #0f172a;
}

.transfer-text {
  color: #2563eb;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.font-mono {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.main-panel {
  background: #ffffff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
  border: 1px solid #f1f5f9;
}

.filter-panel {
  margin-bottom: 20px;
}

.premium-filters :deep(.el-form-item) {
  margin-bottom: 12px;
  margin-right: 16px;
}

.filter-select {
  width: 180px;
}

.filter-date-range {
  width: 260px;
}

.table-container {
  overflow-x: auto;
}

.mono-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 14px;
}

.income-text {
  color: #10b981;
}

.expense-text {
  color: #ef4444;
}

.visual-flow {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;
  padding: 16px 20px;
  background: #f8fafc;
  border-radius: 12px;
  border: 1px solid #e2e8f0;
}

.asset-box {
  flex: 1;
  text-align: center;
  background: #ffffff;
  padding: 12px;
  border-radius: 8px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
  border: 1px solid #e2e8f0;
}

.asset-label {
  font-size: 12px;
  color: #64748b;
  margin-bottom: 4px;
}

.asset-name {
  font-weight: 600;
  color: #1e293b;
  font-size: 14px;
}

.asset-placeholder {
  color: #94a3b8;
  font-style: italic;
  font-size: 13px;
}

.flow-arrow {
  flex: 0 0 110px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  position: relative;
}

.arrow-icon {
  font-size: 20px;
  color: #2563eb;
  background: #eff6ff;
  padding: 6px;
  border-radius: 50%;
}

.flow-amount {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-weight: 600;
  font-size: 12px;
  color: #10b981;
  margin-top: 4px;
}

.transfer-info {
  margin-top: 20px;
  padding: 12px 16px;
  background: #f8fafc;
  border-radius: 8px;
  border-left: 3px solid #3b82f6;
}

.transfer-info h4 {
  display: flex;
  align-items: center;
  gap: 6px;
  color: #334155;
  margin: 0 0 8px 0;
  font-size: 13px;
}

.transfer-info ul {
  margin: 0;
  padding-left: 18px;
  color: #64748b;
  font-size: 12px;
  line-height: 1.5;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}
</style>
