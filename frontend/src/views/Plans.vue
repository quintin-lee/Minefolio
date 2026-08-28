<template>
  <div class="plans-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>计划与日历</h2>
      </div>
      <div class="header-actions">
        <el-button v-if="activeTab === 'dca'" type="primary" class="action-btn" @click="openDcaDialog()">
          <el-icon><Plus /></el-icon> 新增定投计划
        </el-button>
        <el-button v-else type="primary" class="action-btn" @click="openScheduleDialog()">
          <el-icon><Plus /></el-icon> 新增现金流计划
        </el-button>
      </div>
    </div>

    <!-- 顶部主标签页切换 -->
    <el-tabs v-model="activeTab" class="custom-tabs">
      <el-tab-pane label="定投计划 (DCA Plans)" name="dca">
        <!-- 待办待执行提醒横幅 -->
        <div v-if="pendingExecutions.length > 0" class="pending-banner">
          <div class="banner-title">
            <el-icon class="bell-icon"><BellFilled /></el-icon>
            <span>今日有 <strong>{{ pendingExecutions.length }}</strong> 项定投待执行：</span>
          </div>
          <div class="pending-list">
            <div v-for="exec in pendingExecutions" :key="exec.id" class="pending-item">
              <div class="exec-info">
                <span class="plan-name">{{ exec.plan_name }}</span>
                <el-tag size="small" type="info">{{ exec.target_asset_name }}</el-tag>
                <span class="funding-name">扣款: {{ exec.funding_asset_name }}</span>
                <span class="amount-tag">¥{{ Number(exec.planned_amount).toFixed(2) }}</span>
                <span v-if="exec.target_net_value" class="net-val-tag">最新净值: {{ Number(exec.target_net_value).toFixed(4) }}</span>
              </div>
              <div class="exec-actions">
                <el-button type="success" size="small" :loading="confirmingId === exec.id" @click="handleConfirmDca(exec)">
                  一键买入入账
                </el-button>
                <el-button size="small" text @click="handleSkipDca(exec)">
                  跳过
                </el-button>
              </div>
            </div>
          </div>
        </div>

        <!-- 定投计划总览指标 -->
        <el-row :gutter="20" class="summary-cards">
          <el-col :span="6">
            <SummaryCard label="活跃定投计划" :value="`${activePlanCount} 个`" type="highlight" />
          </el-col>
          <el-col :span="6">
            <SummaryCard label="累计定投总投入" :value="`¥${formatNumber(totalDcaInvested)}`" type="expense" />
          </el-col>
          <el-col :span="6">
            <SummaryCard label="定投标的当前总市值" :value="`¥${formatNumber(totalDcaCurrentValue)}`" type="income" />
          </el-col>
          <el-col :span="6">
            <SummaryCard
              label="定投整体浮动盈亏"
              :value="`${totalDcaPnl >= 0 ? '+' : ''}¥${formatNumber(totalDcaPnl)} (${totalDcaPnlPct.toFixed(2)}%)`"
              :type="totalDcaPnl >= 0 ? 'income' : 'expense'"
            />
          </el-col>
        </el-row>

        <!-- 定投计划列表表格 -->
        <div class="table-container">
          <el-table :data="dcaPlans" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
            <el-table-column label="计划名称" min-width="160">
              <template #default="{ row }">
                <div class="plan-name-cell">
                  <span class="plan-title">{{ row.name }}</span>
                  <el-tag v-if="row.status === 'active'" size="small" type="success" effect="plain">运行中</el-tag>
                  <el-tag v-else-if="row.status === 'paused'" size="small" type="warning" effect="plain">已暂停</el-tag>
                  <el-tag v-else size="small" type="info" effect="plain">已完成</el-tag>
                </div>
              </template>
            </el-table-column>

            <el-table-column label="标的与扣款账户" min-width="170">
              <template #default="{ row }">
                <div class="asset-pair-cell">
                  <div class="target-name">
                    <strong>{{ row.target_asset_name }}</strong>
                    <span v-if="row.target_symbol" class="sub-symbol">({{ row.target_symbol }})</span>
                  </div>
                  <div class="funding-name">扣款: {{ row.funding_asset_name }}</div>
                </div>
              </template>
            </el-table-column>

            <el-table-column label="定投周期" width="120">
              <template #default="{ row }">
                <span>{{ formatFrequency(row.frequency, row.day_of_period) }}</span>
              </template>
            </el-table-column>

            <el-table-column label="每期金额" width="120" align="right">
              <template #default="{ row }">
                <span class="mono-amount">¥{{ formatNumber(row.amount) }}</span>
              </template>
            </el-table-column>

            <el-table-column label="累计投入 / 期数" min-width="140" align="right">
              <template #default="{ row }">
                <div class="mono-amount">¥{{ formatNumber(row.total_invested_amount || 0) }}</div>
                <div class="sub-count">{{ row.executed_periods || 0 }} 期</div>
              </template>
            </el-table-column>

            <el-table-column label="目标止盈与收益" min-width="160">
              <template #default="{ row }">
                <div class="profit-status-cell">
                  <div v-if="row.profit_target_reached" class="profit-badge hit">
                    🎉 已达止盈目标 ({{ (row.profit_rate * 100).toFixed(1) }}%)
                  </div>
                  <div v-else-if="row.target_profit_rate > 0" class="profit-badge tracking">
                    目标 +{{ (row.target_profit_rate * 100).toFixed(0) }}% (当前 {{ row.profit_rate ? (row.profit_rate * 100).toFixed(1) : '0.0' }}%)
                  </div>
                  <div v-else class="profit-badge normal">
                    收益率: {{ row.profit_rate ? (row.profit_rate * 100).toFixed(1) : '0.0' }}%
                  </div>
                </div>
              </template>
            </el-table-column>

            <el-table-column label="操作" width="180" align="center">
              <template #default="{ row }">
                <div class="action-buttons">
                  <el-tooltip content="查看执行历史" placement="top">
                    <el-button link type="info" size="small" :icon="Document" @click="openExecutionsHistory(row as any)" />
                  </el-tooltip>
                  <el-tooltip :content="row.status === 'active' ? '暂停计划' : '恢复计划'" placement="top">
                    <el-button
                      link
                      :type="row.status === 'active' ? 'warning' : 'success'"
                      size="small"
                      :icon="row.status === 'active' ? VideoPause : VideoPlay"
                      @click="togglePlanStatus(row as any)"
                    />
                  </el-tooltip>
                  <el-tooltip content="编辑" placement="top">
                    <el-button link type="primary" size="small" :icon="Edit" @click="openDcaDialog(row as any)" />
                  </el-tooltip>
                  <el-tooltip content="删除" placement="top">
                    <el-button link type="danger" size="small" :icon="Delete" @click="handleDeleteDca(row as any)" />
                  </el-tooltip>
                </div>
              </template>
            </el-table-column>
          </el-table>
        </div>
      </el-tab-pane>

      <!-- 现金流日历 Tab -->
      <el-tab-pane label="现金流日历 (Cashflow Calendar)" name="calendar">
        <CashflowCalendar
          ref="calendarRef"
          @open-schedule="scheduleDrawerVisible = true"
          @income-recorded="loadData"
        />
      </el-tab-pane>
    </el-tabs>

    <!-- 定投对话框 -->
    <DcaPlanDialog ref="dcaDialogRef" :assets="allAssets" @success="loadData" />

    <!-- 现金流计划对话框 -->
    <CashflowScheduleDialog ref="scheduleDialogRef" :assets="allAssets" @success="onScheduleSaved" />

    <!-- 现金流计划管理抽屉 -->
    <el-drawer v-model="scheduleDrawerVisible" title="被动现金流计划规则" size="620px">
      <div class="drawer-header-actions">
        <el-button type="primary" size="small" @click="openScheduleDialog()">
          <el-icon><Plus /></el-icon> 新增计划规则
        </el-button>
      </div>
      <el-table :data="schedules" style="width: 100%; margin-top: 12px;">
        <el-table-column prop="name" label="项目名称" min-width="120" />
        <el-table-column label="标的/收款" min-width="140">
          <template #default="{ row }">
            <div>{{ row.source_asset_name }} $\to$ {{ row.target_asset_name }}</div>
          </template>
        </el-table-column>
        <el-table-column prop="frequency" label="频次" width="90">
          <template #default="{ row }">
            <el-tag size="small">{{ formatFlowFreq(row.frequency) }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="预计金额" width="110" align="right">
          <template #default="{ row }">
            <strong style="color: #10b981;">¥{{ formatNumber(row.expected_amount) }}</strong>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="110" align="center">
          <template #default="{ row }">
            <el-button link type="primary" size="small" :icon="Edit" @click="openScheduleDialog(row as any)" />
            <el-button link type="danger" size="small" :icon="Delete" @click="handleDeleteSchedule(row as any)" />
          </template>
        </el-table-column>
      </el-table>
    </el-drawer>

    <!-- 定投执行历史对话框 -->
    <el-dialog v-model="historyDialogVisible" :title="`「${selectedPlan?.name}」定投执行明细`" width="620px" class="premium-dialog">
      <el-table :data="planExecutions" height="350">
        <el-table-column prop="period_date" label="执行日期" width="120" />
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag v-if="row.status === 'confirmed'" size="small" type="success">已买入</el-tag>
            <el-tag v-else-if="row.status === 'skipped'" size="small" type="info">已跳过</el-tag>
            <el-tag v-else size="small" type="warning">待确认</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="买入金额" min-width="110" align="right">
          <template #default="{ row }">
            <span>¥{{ formatNumber(row.actual_amount || row.planned_amount) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="成交净值" min-width="100" align="right">
          <template #default="{ row }">
            <span>{{ row.executed_price ? Number(row.executed_price).toFixed(4) : '-' }}</span>
          </template>
        </el-table-column>
        <el-table-column label="买入份额" min-width="100" align="right">
          <template #default="{ row }">
            <span>{{ row.executed_quantity ? Number(row.executed_quantity).toFixed(2) : '-' }}</span>
          </template>
        </el-table-column>
      </el-table>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus, Edit, Delete, VideoPause, VideoPlay, Document, BellFilled } from '@element-plus/icons-vue'
import SummaryCard from '@/components/SummaryCard.vue'
import DcaPlanDialog from '@/components/DcaPlanDialog.vue'
import CashflowScheduleDialog from '@/components/CashflowScheduleDialog.vue'
import CashflowCalendar from '@/components/CashflowCalendar.vue'
import { dcaApi } from '@/api/dca'
import { cashflowApi } from '@/api/cashflow'
import { assetsApi } from '@/api/assets'
import type { Asset, DcaPlan, DcaExecution, CashflowSchedule } from '@/types'

const activeTab = ref<'dca' | 'calendar'>('dca')
const allAssets = ref<Asset[]>([])
const dcaPlans = ref<DcaPlan[]>([])
const pendingExecutions = ref<DcaExecution[]>([])
const schedules = ref<CashflowSchedule[]>([])
const confirmingId = ref<number | null>(null)

const dcaDialogRef = ref()
const scheduleDialogRef = ref()
const calendarRef = ref()
const scheduleDrawerVisible = ref(false)

const historyDialogVisible = ref(false)
const selectedPlan = ref<DcaPlan | null>(null)
const planExecutions = ref<DcaExecution[]>([])

function formatNumber(num: number) {
  return Number(num || 0).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function formatFrequency(freq: string, dop: number) {
  if (freq === 'weekly') return `每周周${['一','二','三','四','五','六','日'][dop - 1] || dop}`
  if (freq === 'biweekly') return `每双周周${['一','二','三','四','五','六','日'][dop - 1] || dop}`
  return `每月 ${dop} 号`
}

function formatFlowFreq(freq: string) {
  const map: Record<string, string> = {
    once: '单次',
    monthly: '每月',
    quarterly: '每季',
    semi_annual: '每半年',
    annual: '每年'
  }
  return map[freq] || freq
}

const activePlanCount = computed(() =>
  dcaPlans.value.filter(p => p.status === 'active').length
)

const totalDcaInvested = computed(() =>
  dcaPlans.value.reduce((sum, p) => sum + (p.total_invested_amount || 0), 0)
)

const totalDcaCurrentValue = computed(() =>
  dcaPlans.value.reduce((sum, p) => sum + (p.target_current_value || 0), 0)
)

const totalDcaPnl = computed(() =>
  totalDcaCurrentValue.value - totalDcaInvested.value
)

const totalDcaPnlPct = computed(() =>
  totalDcaInvested.value > 0 ? (totalDcaPnl.value / totalDcaInvested.value) * 100 : 0
)

async function loadData() {
  try {
    const [assetsRes, plansRes, pendingRes, schedulesRes] = await Promise.allSettled([
      assetsApi.list({ page: 1, page_size: 500 }),
      dcaApi.listPlans(),
      dcaApi.listPendingExecutions(),
      cashflowApi.listSchedules()
    ])

    if (assetsRes.status === 'fulfilled') allAssets.value = assetsRes.value.list || []
    if (plansRes.status === 'fulfilled') dcaPlans.value = plansRes.value || []
    if (pendingRes.status === 'fulfilled') pendingExecutions.value = pendingRes.value || []
    if (schedulesRes.status === 'fulfilled') schedules.value = schedulesRes.value || []
  } catch (err) {
    console.error('[Plans] loadData failed:', err)
  }
}

function openDcaDialog(plan?: DcaPlan) {
  dcaDialogRef.value?.open(plan)
}

function openScheduleDialog(sch?: CashflowSchedule) {
  scheduleDialogRef.value?.open(sch)
}

function onScheduleSaved() {
  loadData()
  calendarRef.value?.reload()
}

async function handleConfirmDca(exec: DcaExecution) {
  confirmingId.value = exec.id
  try {
    await dcaApi.confirmExecution(exec.id)
    ElMessage.success(`定投计划「${exec.plan_name}」买入记账成功`)
    await loadData()
  } catch (err: any) {
    ElMessage.error(err?.message || '确认执行失败')
  } finally {
    confirmingId.value = null
  }
}

async function handleSkipDca(exec: DcaExecution) {
  await dcaApi.skipExecution(exec.id)
  ElMessage.info('已跳过本期定投')
  loadData()
}

async function togglePlanStatus(plan: DcaPlan) {
  const newStatus = plan.status === 'active' ? 'paused' : 'active'
  await dcaApi.setPlanStatus(plan.id, newStatus)
  ElMessage.success(newStatus === 'active' ? '定投计划已恢复' : '定投计划已暂停')
  loadData()
}

async function handleDeleteDca(plan: DcaPlan) {
  await ElMessageBox.confirm(`确定删除定投计划「${plan.name}」吗？`, '提示', { type: 'warning' })
  await dcaApi.deletePlan(plan.id)
  ElMessage.success('计划已删除')
  loadData()
}

async function openExecutionsHistory(plan: DcaPlan) {
  selectedPlan.value = plan
  try {
    const list = await dcaApi.listExecutions(plan.id)
    planExecutions.value = list || []
    historyDialogVisible.value = true
  } catch (err) {
    console.error('load executions error:', err)
  }
}

async function handleDeleteSchedule(sch: CashflowSchedule) {
  await ElMessageBox.confirm(`确定删除现金流计划「${sch.name}」吗？`, '提示', { type: 'warning' })
  await cashflowApi.deleteSchedule(sch.id)
  ElMessage.success('现金流计划已删除')
  loadData()
  calendarRef.value?.reload()
}

onMounted(() => {
  loadData()
})
</script>

<style scoped>
.plans-page {
  background-color: var(--mf-background);
  height: 100%;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding-bottom: 24px;
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

.action-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
  padding: 10px 16px;
}

.custom-tabs :deep(.el-tabs__nav-wrap::after) {
  height: 1px;
  background-color: var(--mf-border);
}

.pending-banner {
  background: linear-gradient(135deg, rgba(234, 179, 8, 0.12), rgba(245, 158, 11, 0.05));
  border: 1px solid rgba(234, 179, 8, 0.35);
  border-radius: var(--mf-radius-md);
  padding: 12px 16px;
  margin-bottom: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.banner-title {
  font-size: 14px;
  font-weight: 600;
  color: #b45309;
  display: flex;
  align-items: center;
  gap: 6px;
}

.bell-icon {
  font-size: 16px;
}

.pending-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.pending-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-sm);
  padding: 8px 12px;
}

.exec-info {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 13px;
}

.plan-name {
  font-weight: 600;
}

.amount-tag {
  font-family: monospace;
  font-weight: 700;
  color: var(--el-color-primary);
}

.net-val-tag {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.summary-cards {
  margin-bottom: 16px;
}

.table-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
  border: 1px solid var(--mf-border);
}

.plan-name-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.plan-title {
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.asset-pair-cell {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.sub-symbol {
  font-family: monospace;
  font-size: 11px;
  color: var(--el-text-color-secondary);
  margin-left: 4px;
}

.funding-name {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.mono-amount {
  font-family: monospace;
  font-weight: 600;
}

.sub-count {
  font-size: 11px;
  color: var(--el-text-color-secondary);
}

.profit-status-cell {
  display: flex;
  align-items: center;
}

.profit-badge {
  font-size: 12px;
  border-radius: 4px;
  padding: 2px 6px;
}

.profit-badge.hit {
  background: rgba(16, 185, 129, 0.15);
  color: #059669;
  font-weight: 700;
  border: 1px solid #10b981;
}

.profit-badge.tracking {
  background: rgba(59, 130, 246, 0.1);
  color: #2563eb;
}

.profit-badge.normal {
  color: var(--el-text-color-secondary);
}

.action-buttons {
  display: flex;
  justify-content: center;
  gap: 4px;
}

.drawer-header-actions {
  display: flex;
  justify-content: flex-end;
}
</style>
