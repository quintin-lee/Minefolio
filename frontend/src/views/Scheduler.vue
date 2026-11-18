<template>
  <div class="scheduler-page">
    <!-- Header Banner -->
    <div class="page-header">
      <div class="header-intro">
        <h1 class="page-title">
          <Icon icon="ph:clock-countdown-duotone" class="title-icon" />
          定时任务中心 (Scheduler Center)
        </h1>
        <p class="page-subtitle">
          配置自动化定时或市场事件驱动任务，支持 AI 自然语言研报提炼与格式化报表生成，直连个人微信即时推送。
        </p>
      </div>

      <div class="header-actions">
        <el-button
          plain
          @click="openChannelDialog"
        >
          <Icon icon="ph:wechat-logo-duotone" class="btn-icon wechat-btn-icon" />
          微信推送通道
        </el-button>

        <el-button
          plain
          :loading="refreshing"
          @click="refreshAll"
        >
          <Icon icon="ph:arrows-clockwise" class="btn-icon" />
          刷新
        </el-button>

        <el-button
          type="primary"
          @click="openCreateJob"
        >
          <Icon icon="ph:plus-bold" class="btn-icon" />
          新建定时任务
        </el-button>
      </div>
    </div>

    <!-- Dashboard Metric Cards -->
    <div class="metrics-grid">
      <div class="metric-card">
        <div class="metric-icon-wrap primary">
          <Icon icon="ph:play-circle-duotone" class="metric-icon" />
        </div>
        <div class="metric-body">
          <div class="metric-label">运行中任务数</div>
          <div class="metric-value">{{ dashboard?.active_jobs ?? 0 }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-wrap success">
          <Icon icon="ph:lightning-duotone" class="metric-icon" />
        </div>
        <div class="metric-body">
          <div class="metric-label">今日调度触发</div>
          <div class="metric-value">{{ dashboard?.today_runs ?? 0 }} 次</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-wrap warning">
          <Icon icon="ph:hourglass-duotone" class="metric-icon" />
        </div>
        <div class="metric-body">
          <div class="metric-label">下次计划触发</div>
          <div class="metric-value small" :title="dashboard?.next_run_job?.name || '暂无计划任务'">
            {{ dashboard?.next_run_job?.name ? `${dashboard.next_run_job.name}` : '暂无计划' }}
          </div>
          <div v-if="dashboard?.next_run_job?.next_run_at" class="metric-sub">
            {{ dashboard.next_run_job.next_run_at }}
          </div>
        </div>
      </div>

      <div class="metric-card clickable" @click="openChannelDialog">
        <div class="metric-icon-wrap info">
          <Icon icon="ph:bell-ringing-duotone" class="metric-icon" />
        </div>
        <div class="metric-body">
          <div class="metric-label">个人微信通道</div>
          <div class="metric-value small">
            <template v-if="dashboard?.push_channel && dashboard.push_channel.channel_type !== 'none'">
              <span class="channel-name">{{ dashboard.push_channel.channel_name || '已配置' }}</span>
              <el-tag
                size="small"
                :type="dashboard.push_channel.is_active ? 'success' : 'danger'"
                class="channel-tag"
              >
                {{ dashboard.push_channel.is_active ? '已激活' : '已停用' }}
              </el-tag>
            </template>
            <template v-else>
              <span class="channel-empty">未绑定 (点击配置)</span>
            </template>
          </div>
          <div class="metric-sub channel-type-sub">
            {{ formatChannelType(dashboard?.push_channel?.channel_type) }}
          </div>
        </div>
      </div>
    </div>

    <!-- Filter Toolbar -->
    <div class="toolbar-card">
      <div class="search-filter-wrap">
        <el-input
          v-model="searchQuery"
          placeholder="搜索任务名称、描述或 Cron 规则..."
          clearable
          class="search-input"
        >
          <template #prefix>
            <Icon icon="ph:magnifying-glass" class="search-icon" />
          </template>
        </el-input>

        <el-radio-group v-model="selectedType" size="default" class="type-filter">
          <el-radio-button value="all">全部类型</el-radio-button>
          <el-radio-button value="ai_prompt">AI 智能任务</el-radio-button>
          <el-radio-button value="structured_report">结构化报表</el-radio-button>
        </el-radio-group>
      </div>

      <div class="toggle-stats-wrap">
        <el-checkbox v-model="enabledOnly">
          仅显示已启用任务
        </el-checkbox>

        <div class="stats-pills">
          <span class="pill-item">
            共 <b>{{ filteredJobs.length }}</b> 个任务
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item success">
            已启用 <b>{{ enabledJobsCount }}</b>
          </span>
        </div>
      </div>
    </div>

    <!-- Tasks Table -->
    <div v-loading="loading" class="table-container">
      <el-table
        v-if="filteredJobs.length > 0"
        :data="paginatedJobs"
        stripe
        class="jobs-table"
      >
        <el-table-column label="任务名称" min-width="200">
          <template #default="{ row }">
            <div class="job-name-cell">
              <div class="job-icon-wrap" :class="row.job_type">
                <Icon
                  :icon="row.job_type === 'ai_prompt' ? 'ph:sparkle-bold' : 'ph:chart-line-up-bold'"
                  class="job-type-icon"
                />
              </div>
              <div class="job-name-info">
                <div class="job-title-row">
                  <span class="job-name">{{ row.name }}</span>
                  <el-tag
                    size="small"
                    :type="row.job_type === 'ai_prompt' ? 'primary' : 'warning'"
                    effect="plain"
                    class="job-type-tag"
                  >
                    {{ row.job_type === 'ai_prompt' ? 'AI 研报' : '结构报表' }}
                  </el-tag>
                </div>
                <div v-if="row.description" class="job-desc" :title="row.description">
                  {{ row.description }}
                </div>
              </div>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="调度频次 / 触发机制" min-width="160">
          <template #default="{ row }">
            <div class="trigger-cell">
              <div class="trigger-main">
                <Icon
                  :icon="row.trigger_type === 'cron' ? 'ph:clock-bold' : 'ph:lightning-bold'"
                  class="trigger-icon"
                />
                <span class="trigger-text">{{ formatTriggerConfig(row as any) }}</span>
              </div>
              <div v-if="row.trigger_type === 'cron'" class="cron-sub">
                {{ parseCronExpr(row.trigger_config) }}
              </div>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="推送渠道" width="130">
          <template #default="{ row }">
            <el-tag
              size="small"
              :type="getDeliveryTagType(row.delivery_channel)"
              effect="light"
            >
              {{ formatDeliveryChannel(row.delivery_channel) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="最近执行" min-width="160">
          <template #default="{ row }">
            <div v-if="row.last_run_at" class="last-run-cell">
              <div class="last-run-status-row">
                <el-tag
                  size="small"
                  :type="row.last_run_status === 'success' ? 'success' : 'danger'"
                  effect="plain"
                >
                  {{ row.last_run_status === 'success' ? '成功' : '失败' }}
                </el-tag>
                <span v-if="row.last_run_duration_ms" class="duration-hint">
                  {{ row.last_run_duration_ms }}ms
                </span>
              </div>
              <div class="last-run-time">{{ row.last_run_at }}</div>
            </div>
            <span v-else class="text-placeholder">未执行</span>
          </template>
        </el-table-column>

        <el-table-column label="下次触发" min-width="155">
          <template #default="{ row }">
            <span v-if="row.enabled && row.next_run_at" class="next-run-time">
              {{ row.next_run_at }}
            </span>
            <span v-else-if="!row.enabled" class="text-placeholder">已暂停</span>
            <span v-else class="text-placeholder">待调度</span>
          </template>
        </el-table-column>

        <el-table-column label="状态" width="80" align="center">
          <template #default="{ row }">
            <el-switch
              :model-value="Boolean(row.enabled)"
              :loading="togglingIds.has(row.id)"
              @change="(val: string | number | boolean) => handleToggle(row as any, Boolean(val))"
            />
          </template>
        </el-table-column>

        <el-table-column label="操作" width="190" fixed="right">
          <template #default="{ row }">
            <div class="row-actions">
              <el-button
                type="primary"
                link
                size="small"
                :loading="runningIds.has(row.id)"
                @click="handleRunNow(row as any)"
              >
                立即执行
              </el-button>
              <el-button
                type="info"
                link
                size="small"
                @click="openLogs(row as any)"
              >
                日志
              </el-button>
              <el-button
                type="primary"
                link
                size="small"
                @click="openEditJob(row as any)"
              >
                编辑
              </el-button>
              <el-button
                type="danger"
                link
                size="small"
                @click="handleDelete(row as any)"
              >
                删除
              </el-button>
            </div>
          </template>
        </el-table-column>
      </el-table>

      <el-empty
        v-else-if="!loading"
        description="暂无定时任务，点击右上方按钮新建任务"
        class="empty-state"
      >
        <template #extra>
          <el-button type="primary" plain @click="openCreateJob">新建第一个定时任务</el-button>
        </template>
      </el-empty>

      <!-- Pagination -->
      <div v-if="filteredJobs.length > pageSize" class="table-pagination">
        <el-pagination
          v-model:current-page="currentPage"
          v-model:page-size="pageSize"
          :total="filteredJobs.length"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next"
          background
          size="small"
        />
      </div>
    </div>

    <!-- Sub Dialogs and Drawer -->
    <ChannelConfigDialog ref="channelDialogRef" @saved="fetchDashboard" />
    <JobDialog ref="jobDialogRef" @saved="refreshAll" />
    <JobLogDrawer ref="logDrawerRef" />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { schedulerApi } from '@/api/scheduler'
import type { ScheduledJob, SchedulerDashboard, ChannelType, DeliveryChannel } from '@/types'
import ChannelConfigDialog from '@/components/scheduler/ChannelConfigDialog.vue'
import JobDialog from '@/components/scheduler/JobDialog.vue'
import JobLogDrawer from '@/components/scheduler/JobLogDrawer.vue'

const loading = ref(false)
const refreshing = ref(false)
const dashboard = ref<SchedulerDashboard | null>(null)
const jobs = ref<ScheduledJob[]>([])

const searchQuery = ref('')
const selectedType = ref('all')
const enabledOnly = ref(false)
const currentPage = ref(1)
const pageSize = ref(20)

const togglingIds = reactive(new Set<number>())
const runningIds = reactive(new Set<number>())

const channelDialogRef = ref<InstanceType<typeof ChannelConfigDialog>>()
const jobDialogRef = ref<InstanceType<typeof JobDialog>>()
const logDrawerRef = ref<InstanceType<typeof JobLogDrawer>>()

const filteredJobs = computed(() => {
  return jobs.value.filter(job => {
    if (enabledOnly.value && !job.enabled) return false
    if (selectedType.value !== 'all' && job.job_type !== selectedType.value) return false
    if (searchQuery.value.trim()) {
      const q = searchQuery.value.trim().toLowerCase()
      const matchName = job.name.toLowerCase().includes(q)
      const matchDesc = (job.description || '').toLowerCase().includes(q)
      const matchTrig = (job.trigger_config || '').toLowerCase().includes(q)
      if (!matchName && !matchDesc && !matchTrig) return false
    }
    return true
  })
})

const paginatedJobs = computed(() => {
  const start = (currentPage.value - 1) * pageSize.value
  return filteredJobs.value.slice(start, start + pageSize.value)
})

const enabledJobsCount = computed(() => {
  return jobs.value.filter(j => Boolean(j.enabled)).length
})

const fetchDashboard = async () => {
  try {
    const res = await schedulerApi.getDashboard()
    dashboard.value = res
  } catch (err: any) {
    console.error('Failed to fetch scheduler dashboard:', err)
  }
}

const fetchJobs = async () => {
  loading.value = true
  try {
    const res = await schedulerApi.listJobs(1, 100)
    jobs.value = res.items || []
  } catch (err: any) {
    ElMessage.error(err.message || '获取定时任务列表失败')
  } finally {
    loading.value = false
  }
}

const refreshAll = async () => {
  refreshing.value = true
  await Promise.all([fetchDashboard(), fetchJobs()])
  refreshing.value = false
}

const openChannelDialog = () => {
  channelDialogRef.value?.open()
}

const openCreateJob = () => {
  jobDialogRef.value?.open()
}

const openEditJob = (job: ScheduledJob) => {
  jobDialogRef.value?.open(job)
}

const openLogs = (job: ScheduledJob) => {
  logDrawerRef.value?.open(job)
}

const handleToggle = async (job: ScheduledJob, newEnabled: boolean) => {
  togglingIds.add(job.id)
  try {
    await schedulerApi.toggleJob(job.id, newEnabled)
    job.enabled = newEnabled
    ElMessage.success(newEnabled ? `任务「${job.name}」已启用` : `任务「${job.name}」已停用`)
    await fetchDashboard()
  } catch (err: any) {
    ElMessage.error(err.message || '修改任务状态失败')
  } finally {
    togglingIds.delete(job.id)
  }
}

const handleRunNow = async (job: ScheduledJob) => {
  runningIds.add(job.id)
  try {
    const res = await schedulerApi.runJob(job.id)
    ElMessage.success(res?.message || `任务「${job.name}」已触发后台执行，稍后可在日志中查收结果`)
    setTimeout(refreshAll, 1500)
  } catch (err: any) {
    ElMessage.error(err.message || '触发任务失败')
  } finally {
    runningIds.delete(job.id)
  }
}

const handleDelete = async (job: ScheduledJob) => {
  try {
    await ElMessageBox.confirm(`确定删除定时任务「${job.name}」吗？历史运行日志将一并清理。`, '删除确认', {
      type: 'warning',
      confirmButtonText: '确定删除',
      cancelButtonText: '取消',
      confirmButtonClass: 'el-button--danger'
    })

    await schedulerApi.deleteJob(job.id)
    ElMessage.success(`任务「${job.name}」已删除`)
    await refreshAll()
  } catch (err: any) {
    if (err !== 'cancel') {
      ElMessage.error(err.message || '删除失败')
    }
  }
}

const formatChannelType = (type?: ChannelType) => {
  switch (type) {
    case 'serverchan':
      return 'Server酱 (Turbo版)'
    case 'pushdeer':
      return 'PushDeer 微信/APP'
    case 'wechat_work':
      return '企业微信群机器人'
    case 'webhook':
      return '通用 HTTP Webhook'
    default:
      return '尚未配置通知通道'
  }
}

const formatTriggerConfig = (job: ScheduledJob) => {
  if (job.trigger_type === 'market_event') {
    try {
      const cfg = JSON.parse(job.trigger_config || '{}')
      if (cfg.event_name === 'daily_summary_event') return '每日收盘盘后事件'
      if (cfg.event_name === 'price_spike_alert') return '标的异动波动预警'
      if (cfg.event_name === 'dca_execution_event') return '定投划转事件'
      return `事件: ${cfg.event_name}`
    } catch {
      return '市场事件触发'
    }
  }

  try {
    const cfg = JSON.parse(job.trigger_config || '{}')
    const expr = cfg.cron_expr || ''
    if (expr === '0 8 * * *') return '每天早晨 08:00'
    if (expr === '0 18 * * 1-5') return '工作日收盘 18:00'
    if (expr === '0 21 * * *') return '每天晚间 21:00'
    if (expr === '0 9 * * 1') return '每周一早晨 09:00'
    return `Cron 定时调度`
  } catch {
    return '定时调度'
  }
}

const parseCronExpr = (cfgStr?: string) => {
  try {
    const cfg = JSON.parse(cfgStr || '{}')
    return cfg.cron_expr || ''
  } catch {
    return ''
  }
}

const formatDeliveryChannel = (ch: DeliveryChannel) => {
  switch (ch) {
    case 'user_default':
      return '个人微信'
    case 'silent_db':
      return '仅静默记录'
    case 'custom_webhook':
      return '专属 Webhook'
    default:
      return ch
  }
}

const getDeliveryTagType = (ch: DeliveryChannel) => {
  switch (ch) {
    case 'user_default':
      return 'success'
    case 'silent_db':
      return 'info'
    case 'custom_webhook':
      return 'warning'
    default:
      return 'info'
  }
}

onMounted(() => {
  refreshAll()
})
</script>

<style scoped>
.scheduler-page {
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
}

.page-title {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0 0 6px;
}

.title-icon {
  font-size: 28px;
  color: var(--el-color-primary);
}

.page-subtitle {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  margin: 0;
}

.header-actions {
  display: flex;
  gap: 10px;
}

.btn-icon {
  margin-right: 4px;
}

.wechat-btn-icon {
  color: #07c160;
}

/* Metric Cards */
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.metric-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 18px 20px;
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 10px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04);
  transition: transform 0.2s, box-shadow 0.2s;
}

.metric-card.clickable {
  cursor: pointer;
}

.metric-card.clickable:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
}

.metric-icon-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  border-radius: 12px;
  flex-shrink: 0;
}

.metric-icon-wrap.primary {
  background: var(--el-color-primary-light-9);
  color: var(--el-color-primary);
}

.metric-icon-wrap.success {
  background: var(--el-color-success-light-9);
  color: var(--el-color-success);
}

.metric-icon-wrap.warning {
  background: var(--el-color-warning-light-9);
  color: var(--el-color-warning);
}

.metric-icon-wrap.info {
  background: #ecfdf5;
  color: #059669;
}

.metric-icon {
  font-size: 26px;
}

.metric-body {
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.metric-label {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-bottom: 4px;
}

.metric-value {
  font-size: 20px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.metric-value.small {
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.metric-sub {
  font-size: 11px;
  color: var(--el-text-color-placeholder);
  margin-top: 2px;
}

.channel-name {
  font-weight: 600;
}

.channel-empty {
  color: var(--el-text-color-placeholder);
  font-size: 13px;
}

/* Toolbar */
.toolbar-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 14px 18px;
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  gap: 16px;
}

.search-filter-wrap {
  display: flex;
  align-items: center;
  gap: 12px;
  flex: 1;
}

.search-input {
  max-width: 320px;
}

.toggle-stats-wrap {
  display: flex;
  align-items: center;
  gap: 16px;
}

.stats-pills {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--el-text-color-secondary);
}

.stats-pills .pill-item.success b {
  color: var(--el-color-success);
}

.pill-divider {
  color: var(--el-border-color);
}

/* Table */
.table-container {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 12px;
}

.jobs-table {
  width: 100%;
}

.job-name-cell {
  display: flex;
  align-items: center;
  gap: 12px;
}

.job-icon-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border-radius: 8px;
  flex-shrink: 0;
}

.job-icon-wrap.ai_prompt {
  background: #f5f3ff;
  color: #8b5cf6;
}

.job-icon-wrap.structured_report {
  background: #eff6ff;
  color: #3b82f6;
}

.job-type-icon {
  font-size: 20px;
}

.job-name-info {
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.job-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.job-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
}

.job-desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-top: 2px;
}

.trigger-cell {
  display: flex;
  flex-direction: column;
}

.trigger-main {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--el-text-color-primary);
}

.trigger-icon {
  font-size: 15px;
  color: var(--el-color-primary);
}

.cron-sub {
  font-family: monospace;
  font-size: 11px;
  color: var(--el-text-color-placeholder);
  margin-top: 2px;
}

.last-run-cell {
  display: flex;
  flex-direction: column;
}

.last-run-status-row {
  display: flex;
  align-items: center;
  gap: 6px;
}

.duration-hint {
  font-family: monospace;
  font-size: 11px;
  color: var(--el-text-color-placeholder);
}

.last-run-time {
  font-size: 11px;
  color: var(--el-text-color-placeholder);
  margin-top: 2px;
}

.next-run-time {
  font-size: 12px;
  color: var(--el-text-color-regular);
}

.text-placeholder {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
}

.row-actions {
  display: flex;
  align-items: center;
  gap: 4px;
}

.empty-state {
  padding: 40px 0;
}

.table-pagination {
  margin-top: 16px;
  display: flex;
  justify-content: flex-end;
}
</style>
