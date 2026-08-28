<template>
  <div class="cashflow-calendar-container">
    <!-- 顶部概览指标 -->
    <el-row :gutter="16" class="calendar-metrics">
      <el-col :span="8">
        <div class="metric-card actual">
          <div class="metric-label">本月已到账被动收入</div>
          <div class="metric-value">¥{{ formatNumber(summaryData?.actual_total || 0) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="metric-card projected">
          <div class="metric-label">本月预期被动收入</div>
          <div class="metric-value">¥{{ formatNumber(summaryData?.projected_total || 0) }}</div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="metric-card annual">
          <div class="metric-label">全年年化预期被动收益</div>
          <div class="metric-value">¥{{ formatNumber(summaryData?.annual_projected_total || 0) }}</div>
        </div>
      </el-col>
    </el-row>

    <!-- 日历头部控制器 -->
    <div class="calendar-toolbar">
      <div class="month-nav">
        <el-button size="small" :icon="ArrowLeft" @click="changeMonth(-1)">上月</el-button>
        <span class="current-month-label">{{ currentYear }} 年 {{ currentMonth }} 月</span>
        <el-button size="small" :icon="ArrowRight" @click="changeMonth(1)">下月</el-button>
        <el-button size="small" text @click="resetToCurrentMonth">回到当月</el-button>
      </div>

      <div class="legend-tags">
        <span class="legend-item"><span class="dot actual"></span> 已到账</span>
        <span class="legend-item"><span class="dot projected"></span> 预期到账</span>
        <el-button type="primary" size="small" plain @click="$emit('open-schedule')">
          <el-icon><Plus /></el-icon> 管理现金流计划
        </el-button>
      </div>
    </div>

    <!-- 月度网格视图 -->
    <div v-loading="loading" class="calendar-grid">
      <div class="grid-header">
        <span v-for="d in ['一', '二', '三', '四', '五', '六', '日']" :key="d" class="header-cell">周{{ d }}</span>
      </div>

      <div class="grid-body">
        <div
          v-for="(day, index) in calendarDays"
          :key="index"
          class="day-cell"
          :class="{
            'is-empty': !day.date,
            'is-today': day.isToday,
            'has-events': day.events.length > 0
          }"
        >
          <div v-if="day.date" class="day-number">{{ day.dayNum }}</div>
          <div v-if="day.date" class="day-events">
            <div
              v-for="(ev, evIdx) in day.events"
              :key="evIdx"
              class="event-pill"
              :class="ev.is_actual ? 'is-actual' : 'is-projected'"
              @click="handleEventClick(ev)"
            >
              <span class="event-type-dot"></span>
              <span class="event-name" :title="ev.name">{{ ev.name }}</span>
              <span class="event-amt">+¥{{ formatNumber(ev.amount) }}</span>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 一键确认入账弹窗 -->
    <el-dialog
      v-model="confirmDialogVisible"
      title="确认现金流收益入账"
      width="460px"
      class="premium-dialog"
      destroy-on-close
    >
      <div v-if="selectedEvent" class="confirm-content">
        <el-descriptions :column="1" border size="small">
          <el-descriptions-item label="收益项目">{{ selectedEvent.name }}</el-descriptions-item>
          <el-descriptions-item label="预期到账日期">{{ selectedEvent.date }}</el-descriptions-item>
          <el-descriptions-item label="预计金额">
            <strong style="color: #10b981;">¥{{ Number(selectedEvent.amount).toFixed(2) }} {{ selectedEvent.currency }}</strong>
          </el-descriptions-item>
          <el-descriptions-item label="收款账户">{{ selectedEvent.target_asset_name || '资金账户' }}</el-descriptions-item>
        </el-descriptions>

        <el-form label-width="90px" style="margin-top: 16px;">
          <el-form-item label="实收金额">
            <el-input-number v-model="confirmAmount" :precision="2" :min="0.01" style="width: 100%" :controls="false" />
          </el-form-item>
          <el-form-item label="实际日期">
            <el-date-picker v-model="confirmDate" type="date" value-format="YYYY-MM-DD" style="width: 100%" />
          </el-form-item>
        </el-form>
      </div>
      <template #footer>
        <div class="dialog-footer">
          <el-button @click="confirmDialogVisible = false">取消</el-button>
          <el-button type="success" :loading="confirming" @click="submitConfirmIncome">
            确认入账并记账
          </el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { ArrowLeft, ArrowRight, Plus } from '@element-plus/icons-vue'
import { cashflowApi } from '@/api/cashflow'
import type { MonthlyCashflowSummary, CashflowCalendarEvent } from '@/types'

const emit = defineEmits<{
  (e: 'open-schedule'): void
  (e: 'income-recorded'): void
}>()

const now = new Date()
const currentYear = ref(now.getFullYear())
const currentMonth = ref(now.getMonth() + 1)
const loading = ref(false)
const summaryData = ref<MonthlyCashflowSummary | null>(null)

const confirmDialogVisible = ref(false)
const selectedEvent = ref<CashflowCalendarEvent | null>(null)
const confirmAmount = ref(0)
const confirmDate = ref('')
const confirming = ref(false)

function formatNumber(num: number) {
  return Number(num || 0).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

async function loadCalendar() {
  loading.value = true
  try {
    const res = await cashflowApi.getCalendar(currentYear.value, currentMonth.value)
    summaryData.value = res
  } catch (err) {
    console.error('[CashflowCalendar] load error:', err)
  } finally {
    loading.value = false
  }
}

function changeMonth(delta: number) {
  let m = currentMonth.value + delta
  let y = currentYear.value
  if (m < 1) {
    m = 12
    y -= 1
  } else if (m > 12) {
    m = 1
    y += 1
  }
  currentYear.value = y
  currentMonth.value = m
  loadCalendar()
}

function resetToCurrentMonth() {
  const d = new Date()
  currentYear.value = d.getFullYear()
  currentMonth.value = d.getMonth() + 1
  loadCalendar()
}

interface CalendarDay {
  date: string
  dayNum: number
  isToday: boolean
  events: CashflowCalendarEvent[]
}

const calendarDays = computed<CalendarDay[]>(() => {
  const y = currentYear.value
  const m = currentMonth.value
  const firstDay = new Date(y, m - 1, 1)
  const lastDay = new Date(y, m, 0)
  const totalDays = lastDay.getDate()

  /* Monday is index 0 in Chinese calendar */
  let startOffset = firstDay.getDay() - 1
  if (startOffset < 0) startOffset = 6

  const todayStr = new Date().toISOString().slice(0, 10)
  const days: CalendarDay[] = []

  /* Fill leading empty days */
  for (let i = 0; i < startOffset; i++) {
    days.push({ date: '', dayNum: 0, isToday: false, events: [] })
  }

  const eventsMap = new Map<string, CashflowCalendarEvent[]>()
  if (summaryData.value?.events) {
    for (const ev of summaryData.value.events) {
      const list = eventsMap.get(ev.date) || []
      list.push(ev)
      eventsMap.set(ev.date, list)
    }
  }

  /* Fill actual month days */
  for (let d = 1; d <= totalDays; d++) {
    const dateStr = `${y}-${String(m).padStart(2, '0')}-${String(d).padStart(2, '0')}`
    days.push({
      date: dateStr,
      dayNum: d,
      isToday: dateStr === todayStr,
      events: eventsMap.get(dateStr) || []
    })
  }

  return days
})

function handleEventClick(ev: CashflowCalendarEvent) {
  if (ev.is_actual) {
    ElMessage.info(`流水「${ev.name}」已于 ${ev.date} 确认入账`)
    return
  }
  selectedEvent.value = ev
  confirmAmount.value = ev.amount
  confirmDate.value = ev.date
  confirmDialogVisible.value = true
}

async function submitConfirmIncome() {
  if (!selectedEvent.value || !selectedEvent.value.target_asset_id) return
  confirming.value = true
  try {
    await cashflowApi.confirmIncome({
      target_asset_id: selectedEvent.value.target_asset_id,
      source_asset_id: selectedEvent.value.source_asset_id,
      amount: confirmAmount.value,
      date: confirmDate.value,
      name: selectedEvent.value.name
    })
    ElMessage.success('被动收入确认入账成功')
    confirmDialogVisible.value = false
    loadCalendar()
    emit('income-recorded')
  } catch (err: any) {
    ElMessage.error(err?.message || '确认入账失败')
  } finally {
    confirming.value = false
  }
}

onMounted(() => {
  loadCalendar()
})

defineExpose({ reload: loadCalendar })
</script>

<style scoped>
.cashflow-calendar-container {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.calendar-metrics {
  margin-bottom: 4px;
}

.metric-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-md);
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.metric-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.metric-value {
  font-size: 20px;
  font-weight: 700;
  font-family: monospace;
}

.metric-card.actual .metric-value {
  color: #10b981;
}

.metric-card.projected .metric-value {
  color: #3b82f6;
}

.metric-card.annual .metric-value {
  color: #8b5cf6;
}

.calendar-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.month-nav {
  display: flex;
  align-items: center;
  gap: 12px;
}

.current-month-label {
  font-size: 16px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.legend-tags {
  display: flex;
  align-items: center;
  gap: 16px;
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 6px;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.dot.actual {
  background-color: #10b981;
}

.dot.projected {
  background-color: #3b82f6;
}

.calendar-grid {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  overflow: hidden;
}

.grid-header {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  background: rgba(15, 23, 42, 0.03);
  border-bottom: 1px solid var(--mf-border);
}

.header-cell {
  text-align: center;
  padding: 10px 0;
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-secondary);
}

.grid-body {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
}

.day-cell {
  min-height: 90px;
  border-right: 1px solid var(--mf-border);
  border-bottom: 1px solid var(--mf-border);
  padding: 8px;
  display: flex;
  flex-direction: column;
  transition: background 0.15s ease;
}

.day-cell:nth-child(7n) {
  border-right: none;
}

.day-cell.is-empty {
  background-color: rgba(0, 0, 0, 0.015);
}

.day-cell.is-today {
  background-color: rgba(59, 130, 246, 0.06);
}

.day-number {
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-regular);
  margin-bottom: 4px;
}

.day-cell.is-today .day-number {
  color: #3b82f6;
}

.day-events {
  display: flex;
  flex-direction: column;
  gap: 4px;
  overflow-y: auto;
}

.event-pill {
  font-size: 11px;
  padding: 3px 6px;
  border-radius: 4px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  cursor: pointer;
  transition: transform 0.1s ease;
}

.event-pill:hover {
  transform: scale(1.02);
}

.event-pill.is-actual {
  background-color: rgba(16, 185, 129, 0.15);
  color: #059669;
  border-left: 2px solid #10b981;
}

.event-pill.is-projected {
  background-color: rgba(59, 130, 246, 0.15);
  color: #2563eb;
  border-left: 2px solid #3b82f6;
}

.event-name {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 65px;
}

.event-amt {
  font-family: monospace;
  font-weight: 600;
}
</style>
