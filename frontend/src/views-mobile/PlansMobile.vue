<template>
  <div class="plans-mobile">
    <div class="page-header">
      <h2>计划与日历</h2>
      <el-button size="small" type="primary" @click="activeTab === 'dca' ? openDcaModal() : openScheduleModal()">
        + 新增{{ activeTab === 'dca' ? '定投' : '现金流' }}
      </el-button>
    </div>

    <!-- 顶部 Tab 切换 -->
    <div class="tab-switch">
      <button :class="{ active: activeTab === 'dca' }" @click="activeTab = 'dca'">
        🎯 定投计划 ({{ dcaPlans.length }})
      </button>
      <button :class="{ active: activeTab === 'cashflow' }" @click="activeTab = 'cashflow'">
        📅 现金流日历 ({{ calendarEvents.length }})
      </button>
    </div>

    <!-- DCA 待办执行横幅 -->
    <div v-if="activeTab === 'dca' && pendingExecutions.length > 0" class="pending-banner">
      <div class="banner-hdr">
        <span>⚡ 今日待执行定投 ({{ pendingExecutions.length }})</span>
      </div>
      <div v-for="exec in pendingExecutions" :key="exec.id" class="pending-card">
        <div class="p-info">
          <div class="p-title">{{ exec.plan_name }}</div>
          <div class="p-sub">{{ exec.target_asset_name }} · ¥{{ Number(exec.planned_amount).toFixed(2) }}</div>
        </div>
        <div class="p-actions">
          <el-button size="small" type="success" :loading="confirmingId === exec.id" @click="handleConfirmDca(exec)">
            执行
          </el-button>
          <el-button size="small" text @click="handleSkipDca(exec)">
            跳过
          </el-button>
        </div>
      </div>
    </div>

    <!-- DCA 定投卡片列表 -->
    <div v-if="activeTab === 'dca'" class="card-list" v-loading="loading">
      <el-empty v-if="dcaPlans.length === 0 && !loading" description="暂无定投计划，点击右上角新增" />

      <div v-for="plan in dcaPlans" :key="plan.id" class="plan-card">
        <div class="card-top">
          <div class="plan-name-wrap">
            <span class="plan-name">{{ plan.name }}</span>
            <el-tag size="small" :type="plan.status === 'active' ? 'success' : 'info'">
              {{ plan.status === 'active' ? '运行中' : '已暂停' }}
            </el-tag>
          </div>
          <div class="plan-amount">¥{{ Number(plan.amount).toFixed(2) }} <span class="freq">/ {{ formatFreq(plan.frequency) }}</span></div>
        </div>

        <div class="card-meta">
          <div class="meta-row">
            <span class="lbl">标的:</span> <span class="val">{{ plan.target_asset_name }}</span>
            <span class="lbl ml">扣款:</span> <span class="val">{{ plan.funding_asset_name }}</span>
          </div>
          <div class="meta-row">
            <span class="lbl">已投期数:</span> <span class="val">{{ plan.executed_periods || 0 }} 期</span>
            <span class="lbl ml">累计投入:</span> <span class="val highlight">¥{{ Number(plan.total_invested_amount || 0).toFixed(2) }}</span>
          </div>
        </div>

        <div class="card-actions">
          <el-button size="small" text type="primary" @click="toggleDcaStatus(plan)">
            {{ plan.status === 'active' ? '暂停' : '恢复' }}
          </el-button>
          <el-button size="small" text type="danger" @click="handleDeleteDca(plan)">
            删除
          </el-button>
        </div>
      </div>
    </div>

    <!-- 现金流日历卡片列表 -->
    <div v-else class="card-list" v-loading="loading">
      <!-- 月度预测汇总 -->
      <div v-if="cashflowSummary" class="cashflow-summary-card">
        <div class="s-title">{{ cashflowSummary.year_month }} 现金流预测</div>
        <div class="s-grid">
          <div class="s-item">
            <span class="s-lbl">已确认入账</span>
            <span class="s-val income">¥{{ Number(cashflowSummary.actual_total || 0).toFixed(2) }}</span>
          </div>
          <div class="s-item">
            <span class="s-lbl">预计待入账</span>
            <span class="s-val highlight">¥{{ Number(cashflowSummary.projected_total || 0).toFixed(2) }}</span>
          </div>
        </div>
      </div>

      <el-empty v-if="calendarEvents.length === 0 && !loading" description="本月暂无现金流计划" />

      <div v-for="evt in calendarEvents" :key="`${evt.schedule_id}-${evt.date}`" class="plan-card">
        <div class="card-top">
          <div class="plan-name-wrap">
            <span class="plan-name">{{ evt.name }}</span>
            <el-tag size="small" :type="evt.is_actual ? 'success' : 'warning'">
              {{ evt.is_actual ? '已入账' : '预测中' }}
            </el-tag>
          </div>
          <div class="plan-amount" :class="evt.amount >= 0 ? 'income' : 'expense'">
            {{ evt.amount >= 0 ? '+' : '' }}¥{{ Number(evt.amount).toFixed(2) }}
          </div>
        </div>

        <div class="card-meta">
          <div class="meta-row">
            <span class="lbl">日期:</span> <span class="val">{{ evt.date }}</span>
            <span class="lbl ml">类型:</span> <span class="val">{{ formatFlowType(evt.flow_type) }}</span>
          </div>
          <div class="meta-row">
            <span class="lbl">来源:</span> <span class="val">{{ evt.source_asset_name || '-' }}</span>
            <span class="lbl ml">收款:</span> <span class="val">{{ evt.target_asset_name || '-' }}</span>
          </div>
        </div>

        <div v-if="!evt.is_actual" class="card-actions">
          <el-button size="small" type="primary" text @click="handleConfirmCashflow(evt)">
            一键入账确认
          </el-button>
        </div>
      </div>
    </div>

    <!-- DCA 创建抽屉 -->
    <el-dialog v-model="dcaModalOpen" title="新建定投计划" width="90%" :append-to-body="true">
      <el-form label-position="top">
        <el-form-item label="计划名称">
          <el-input v-model="dcaForm.name" placeholder="如: 沪深300指数定投" />
        </el-form-item>
        <el-form-item label="标的资产 (购买)">
          <el-select v-model="dcaForm.target_asset_id" placeholder="选择资产" style="width: 100%">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="a.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="扣款账户 (出资)">
          <el-select v-model="dcaForm.funding_asset_id" placeholder="选择出资账户" style="width: 100%">
            <el-option v-for="a in assets" :key="a.id" :label="a.name" :value="a.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="定投金额 (元)">
          <el-input-number v-model="dcaForm.amount" :min="1" :step="100" style="width: 100%" />
        </el-form-item>
        <el-form-item label="执行周期">
          <el-select v-model="dcaForm.frequency" style="width: 100%">
            <el-option label="每周" value="weekly" />
            <el-option label="每双周" value="biweekly" />
            <el-option label="每月" value="monthly" />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dcaModalOpen = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submitDca">创建</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { dcaApi } from '@/api/dca'
import { cashflowApi } from '@/api/cashflow'
import { assetsApi } from '@/api/assets'
import type { DcaPlan, DcaExecution, CashflowCalendarEvent, MonthlyCashflowSummary, Asset } from '@/types'

const activeTab = ref<'dca' | 'cashflow'>('dca')
const loading = ref(false)
const saving = ref(false)
const confirmingId = ref<number | null>(null)

const dcaPlans = ref<DcaPlan[]>([])
const pendingExecutions = ref<DcaExecution[]>([])
const calendarEvents = ref<CashflowCalendarEvent[]>([])
const cashflowSummary = ref<MonthlyCashflowSummary | null>(null)
const assets = ref<Asset[]>([])

const dcaModalOpen = ref(false)
const dcaForm = ref({
  name: '',
  target_asset_id: undefined as number | undefined,
  funding_asset_id: undefined as number | undefined,
  amount: 1000,
  frequency: 'monthly',
})

function formatFreq(f: string) {
  const map: Record<string, string> = { daily: '每天', weekly: '每周', biweekly: '双周', monthly: '每月' }
  return map[f] || f
}

function formatFlowType(t: string) {
  const map: Record<string, string> = { dividend: '分红股息', interest: '利息收益', rental: '房租收入', salary: '工资入账', other_income: '其他收入' }
  return map[t] || t
}

async function loadData() {
  loading.value = true
  try {
    const [plansRes, pendRes, cfRes, astRes] = await Promise.allSettled([
      dcaApi.listPlans(),
      dcaApi.listPendingExecutions(),
      cashflowApi.getCalendar(),
      assetsApi.list(),
    ])

    if (plansRes.status === 'fulfilled') dcaPlans.value = plansRes.value || []
    if (pendRes.status === 'fulfilled') pendingExecutions.value = pendRes.value || []
    if (cfRes.status === 'fulfilled') {
      cashflowSummary.value = cfRes.value
      calendarEvents.value = cfRes.value?.events || []
    }
    if (astRes.status === 'fulfilled') {
      assets.value = Array.isArray(astRes.value) ? astRes.value : (astRes.value as any)?.list || []
    }
  } catch (err: any) {
    ElMessage.error(err.message || '加载计划数据失败')
  } finally {
    loading.value = false
  }
}

async function handleConfirmDca(exec: DcaExecution) {
  confirmingId.value = exec.id
  try {
    await dcaApi.confirmExecution(exec.id)
    ElMessage.success('定投买入成功入账！')
    await loadData()
  } catch (e: any) {
    ElMessage.error(e.message || '执行失败')
  } finally {
    confirmingId.value = null
  }
}

async function handleSkipDca(exec: DcaExecution) {
  try {
    await dcaApi.skipExecution(exec.id)
    ElMessage.info('已跳过本期定投')
    await loadData()
  } catch (e: any) {
    ElMessage.error(e.message || '操作失败')
  }
}

async function toggleDcaStatus(plan: DcaPlan) {
  const newStatus = plan.status === 'active' ? 'paused' : 'active'
  try {
    await dcaApi.updatePlan(plan.id, { status: newStatus })
    ElMessage.success(newStatus === 'active' ? '计划已恢复' : '计划已暂停')
    await loadData()
  } catch (e: any) {
    ElMessage.error(e.message || '更新失败')
  }
}

async function handleDeleteDca(plan: DcaPlan) {
  try {
    await ElMessageBox.confirm(`确定删除定投计划 "${plan.name}" 吗？`, '删除确认', { type: 'warning' })
    await dcaApi.deletePlan(plan.id)
    ElMessage.success('定投计划已删除')
    await loadData()
  } catch {}
}

async function handleConfirmCashflow(evt: CashflowCalendarEvent) {
  if (!evt.target_asset_id) {
    ElMessage.warning('现金流事件缺少收款账户')
    return
  }
  try {
    await cashflowApi.confirmIncome({
      target_asset_id: evt.target_asset_id,
      source_asset_id: evt.source_asset_id,
      amount: evt.amount,
      date: evt.date,
      name: evt.name,
    })
    ElMessage.success('现金流已确认入账！')
    await loadData()
  } catch (e: any) {
    ElMessage.error(e.message || '入账失败')
  }
}

function openDcaModal() {
  dcaForm.value = {
    name: '',
    target_asset_id: assets.value[0]?.id,
    funding_asset_id: assets.value[1]?.id || assets.value[0]?.id,
    amount: 1000,
    frequency: 'monthly',
  }
  dcaModalOpen.value = true
}

function openScheduleModal() {
  ElMessage.info('可通过桌面端管理更复杂的现金流周期排程')
}

async function submitDca() {
  if (!dcaForm.value.name || !dcaForm.value.target_asset_id || !dcaForm.value.funding_asset_id) {
    ElMessage.warning('请填写完整的计划名称与出入账户')
    return
  }
  saving.value = true
  try {
    await dcaApi.createPlan({
      name: dcaForm.value.name,
      target_asset_id: dcaForm.value.target_asset_id,
      funding_asset_id: dcaForm.value.funding_asset_id,
      amount: dcaForm.value.amount,
      frequency: dcaForm.value.frequency as 'weekly' | 'biweekly' | 'monthly',
      status: 'active',
    })
    ElMessage.success('定投计划创建成功！')
    dcaModalOpen.value = false
    await loadData()
  } catch (e: any) {
    ElMessage.error(e.message || '创建失败')
  } finally {
    saving.value = false
  }
}

onMounted(() => {
  loadData()
})
</script>

<style scoped>
.plans-mobile {
  padding-bottom: 24px;
}
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
}
.page-header h2 {
  font-size: 20px;
  font-weight: 700;
  color: var(--mf-text-primary);
  margin: 0;
}
.tab-switch {
  display: flex;
  background: var(--mf-card-bg);
  border: 1px solid var(--mf-border);
  border-radius: 10px;
  padding: 4px;
  margin-bottom: 14px;
}
.tab-switch button {
  flex: 1;
  border: none;
  background: transparent;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-secondary);
  cursor: pointer;
  transition: all 0.2s;
}
.tab-switch button.active {
  background: var(--mf-primary);
  color: #fff;
}
.pending-banner {
  background: rgba(245, 158, 11, 0.1);
  border: 1px solid rgba(245, 158, 11, 0.3);
  border-radius: 12px;
  padding: 12px;
  margin-bottom: 14px;
}
.banner-hdr {
  font-size: 13px;
  font-weight: 700;
  color: #f59e0b;
  margin-bottom: 8px;
}
.pending-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: var(--mf-card-bg);
  padding: 10px;
  border-radius: 8px;
  margin-top: 6px;
}
.p-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-text-primary);
}
.p-sub {
  font-size: 12px;
  color: var(--mf-text-secondary);
}
.card-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.cashflow-summary-card {
  background: linear-gradient(135deg, rgba(59, 130, 246, 0.1), rgba(16, 185, 129, 0.1));
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 14px;
}
.s-title {
  font-size: 14px;
  font-weight: 700;
  color: var(--mf-text-primary);
  margin-bottom: 10px;
}
.s-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.s-lbl {
  display: block;
  font-size: 11px;
  color: var(--mf-text-secondary);
}
.s-val {
  font-size: 16px;
  font-weight: 700;
}
.income { color: #10b981; }
.expense { color: #ef4444; }
.highlight { color: #3b82f6; }
.plan-card {
  background: var(--mf-card-bg);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 14px;
}
.card-top {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 10px;
}
.plan-name-wrap {
  display: flex;
  align-items: center;
  gap: 6px;
}
.plan-name {
  font-size: 15px;
  font-weight: 600;
  color: var(--mf-text-primary);
}
.plan-amount {
  font-size: 16px;
  font-weight: 700;
  color: var(--mf-text-primary);
}
.freq {
  font-size: 12px;
  font-weight: normal;
  color: var(--mf-text-secondary);
}
.card-meta {
  font-size: 12px;
  color: var(--mf-text-secondary);
  border-top: 1px dashed var(--mf-border);
  padding-top: 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.meta-row {
  display: flex;
  align-items: center;
}
.lbl { color: var(--mf-text-tertiary); }
.val { color: var(--mf-text-secondary); margin-left: 4px; }
.ml { margin-left: 12px; }
.card-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 8px;
  padding-top: 6px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}
</style>
