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
          <div class="metric-value tabular-nums">{{ dashboard?.active_jobs ?? 0 }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-wrap success">
          <Icon icon="ph:lightning-duotone" class="metric-icon" />
        </div>
        <div class="metric-body">
          <div class="metric-label">今日调度触发</div>
          <div class="metric-value tabular-nums">{{ dashboard?.today_runs ?? 0 }} <span class="unit-text">次</span></div>
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
          <div v-if="dashboard?.next_run_job?.next_run_at" class="metric-sub tabular-nums">
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
                :type="pushChannelTagType"
                class="channel-tag"
              >
                {{ pushChannelTagLabel }}
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
          <el-radio-button value="all">全部任务</el-radio-button>
          <el-radio-button value="system_builtin">系统内置</el-radio-button>
          <el-radio-button value="custom">自定义任务</el-radio-button>
          <el-radio-button value="ai_prompt">AI 研报</el-radio-button>
          <el-radio-button value="structured_report">结构报表</el-radio-button>
        </el-radio-group>
      </div>

      <div class="toggle-stats-wrap">
        <el-checkbox v-model="enabledOnly">
          仅显示已启用任务
        </el-checkbox>

        <div class="stats-pills">
          <span class="pill-item">
            共 <b>{{ jobsTotal }}</b> 个任务
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item builtin">
            内置 <b>{{ builtinJobsCount }}</b>
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item custom">
            自定义 <b>{{ customJobsCount }}</b>
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
        :data="filteredJobs"
        height="100%"
        class="premium-table jobs-table"
        row-class-name="premium-row"
        header-cell-class-name="premium-header"
      >
        <el-table-column label="任务名称" min-width="230">
          <template #default="{ row }">
            <div class="job-name-cell">
              <div class="job-icon-wrap" :class="row.is_builtin ? 'system_builtin' : row.job_type">
                <Icon
                  :icon="row.is_builtin ? 'ph:gear-duotone' : (row.job_type === 'ai_prompt' ? 'ph:sparkle-bold' : 'ph:chart-line-up-bold')"
                  class="job-type-icon"
                />
              </div>
              <div class="job-name-info">
                <div class="job-title-row">
                  <span class="job-name" :title="row.name">{{ row.name }}</span>
                  <el-tag
                    v-if="row.is_builtin"
                    size="small"
                    type="info"
                    effect="plain"
                    class="job-type-tag builtin-badge"
                  >
                    <Icon icon="ph:shield-check-bold" class="badge-icon" /> 系统内置
                  </el-tag>
                  <el-tag
                    v-else
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

        <el-table-column label="调度频次 / 触发机制" min-width="170">
          <template #default="{ row }">
            <div class="trigger-cell">
              <div class="trigger-main">
                <Icon
                  :icon="row.trigger_type === 'cron' ? 'ph:clock-bold' : 'ph:lightning-bold'"
                  class="trigger-icon"
                />
                <span class="trigger-text">{{ formatTriggerConfig(row as any) }}</span>
              </div>
              <div v-if="row.trigger_type === 'cron'" class="cron-sub tabular-nums">
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

        <el-table-column label="最近执行" min-width="170">
          <template #default="{ row }">
            <div v-if="row.last_run_at" class="last-run-cell">
              <div class="last-run-status-row">
                <el-tag
                  v-if="row.last_run_status === 'running'"
                  size="small"
                  type="primary"
                  effect="light"
                  class="running-tag"
                >
                  <Icon icon="ph:spinner-gap-bold" class="spin-icon" />
                  执行中
                </el-tag>
                <el-tag
                  v-else-if="row.last_run_status === 'success'"
                  size="small"
                  type="success"
                  effect="plain"
                >
                  成功
                </el-tag>
                <el-tag
                  v-else-if="row.last_run_status === 'failed'"
                  size="small"
                  type="danger"
                  effect="plain"
                >
                  失败
                </el-tag>
                <el-tag
                  v-else
                  size="small"
                  type="info"
                  effect="plain"
                >
                  {{ row.last_run_status }}
                </el-tag>
                <span v-if="row.last_run_duration_ms" class="duration-hint tabular-nums">
                  {{ row.last_run_duration_ms }}ms
                </span>
              </div>
              <div class="last-run-time tabular-nums">{{ row.last_run_at }}</div>
            </div>
            <span v-else class="text-placeholder">未执行</span>
          </template>
        </el-table-column>

        <el-table-column label="下次触发" min-width="160">
          <template #default="{ row }">
            <span v-if="row.enabled && row.next_run_at" class="next-run-time tabular-nums">
              {{ row.next_run_at }}
            </span>
            <span v-else-if="!row.enabled" class="text-placeholder">已暂停</span>
            <span v-else class="text-placeholder">待调度</span>
          </template>
        </el-table-column>

        <el-table-column label="状态" width="85" align="center">
          <template #default="{ row }">
            <el-switch
              :model-value="Boolean(row.enabled)"
              :loading="togglingIds.has(row.id)"
              @change="(val: string | number | boolean) => handleToggle(row as any, Boolean(val))"
            />
          </template>
        </el-table-column>

        <el-table-column label="操作" width="168" fixed="right">
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
                v-if="!row.is_builtin"
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
      <div v-if="jobsTotal > 0" class="table-pagination">
        <el-pagination
          v-model:current-page="currentPage"
          v-model:page-size="pageSize"
          :total="jobsTotal"
          :page-sizes="[10, 20, 50]"
          layout="total, sizes, prev, pager, next"
          background
          size="small"
          @current-change="fetchJobs"
          @size-change="handlePageSizeChange"
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
import { ref, reactive, computed, onMounted, onBeforeUnmount } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { schedulerApi } from '@/api/scheduler'
import { classifyIlinkChannelStatus, getIlinkStatusLabel } from '@/utils/ilink-channel'
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
const jobsTotal = ref(0)

const togglingIds = reactive(new Set<number>())
const runningIds = reactive(new Set<number>())
const deletingIds = reactive(new Set<number>())
let jobsReqSeq = 0
let runRefreshTimer: ReturnType<typeof setTimeout> | null = null
let isUnmounted = false

const channelDialogRef = ref<InstanceType<typeof ChannelConfigDialog>>()
const jobDialogRef = ref<InstanceType<typeof JobDialog>>()
const logDrawerRef = ref<InstanceType<typeof JobLogDrawer>>()

const ilinkChannelReadiness = computed(() => {
  const ch = dashboard.value?.push_channel
  if (!ch || ch.channel_type !== 'wechat_ilink') return null
  return classifyIlinkChannelStatus(ch.ilink_status)
})

const pushChannelTagType = computed<'success' | 'warning' | 'danger'>(() => {
  const ch = dashboard.value?.push_channel
  if (!ch || !ch.is_active) return 'danger'
  if (ch.channel_type === 'wechat_ilink' && ilinkChannelReadiness.value) {
    return ilinkChannelReadiness.value.level
  }
  return 'success'
})

const pushChannelTagLabel = computed(() => {
  const ch = dashboard.value?.push_channel
  if (!ch || !ch.is_active) return '已停用'
  if (ch.channel_type === 'wechat_ilink' && ilinkChannelReadiness.value) {
    return ilinkChannelReadiness.value.reason === 'not_bound'
      ? '未绑定'
      : getIlinkStatusLabel(ilinkChannelReadiness.value.reason)
  }
  return '已激活'
})

const filteredJobs = computed(() => {
  return jobs.value.filter(job => {
    if (enabledOnly.value && !job.enabled) return false
    if (selectedType.value === 'system_builtin') {
      if (!job.is_builtin) return false
    } else if (selectedType.value === 'custom') {
      if (job.is_builtin) return false
    } else if (selectedType.value === 'ai_prompt') {
      if (job.job_type !== 'ai_prompt' || job.is_builtin) return false
    } else if (selectedType.value === 'structured_report') {
      if (job.job_type !== 'structured_report' || job.is_builtin) return false
    }
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

const enabledJobsCount = computed(() => {
  return jobs.value.filter(j => Boolean(j.enabled)).length
})

const builtinJobsCount = computed(() => {
  return jobs.value.filter(j => Boolean(j.is_builtin)).length
})

const customJobsCount = computed(() => {
  return jobs.value.filter(j => !j.is_builtin).length
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
  const reqId = ++jobsReqSeq
  loading.value = true
  try {
    const res = await schedulerApi.listJobs(currentPage.value, pageSize.value)
    if (isUnmounted || reqId !== jobsReqSeq) return
    jobs.value = res.items || []
    jobsTotal.value = typeof res.total === 'number' ? res.total : jobs.value.length
  } catch (err: any) {
    if (isUnmounted || reqId !== jobsReqSeq) return
    ElMessage.error(err.message || '获取定时任务列表失败')
  } finally {
    if (!isUnmounted && reqId === jobsReqSeq) {
      loading.value = false
    }
  }
}

const handlePageSizeChange = () => {
  currentPage.value = 1
  fetchJobs()
}

const refreshAll = async () => {
  if (isUnmounted) return
  refreshing.value = true
  try {
    await Promise.all([fetchDashboard(), fetchJobs()])
  } finally {
    if (!isUnmounted) {
      refreshing.value = false
    }
  }
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
  if (togglingIds.has(job.id)) return
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
  if (runningIds.has(job.id)) return
  runningIds.add(job.id)
  try {
    const res = await schedulerApi.runJob(job.id)
    ElMessage.success(res?.message || `任务「${job.name}」已触发后台执行，稍后可在日志中查收结果`)
    if (runRefreshTimer) clearTimeout(runRefreshTimer)
    runRefreshTimer = setTimeout(() => {
      if (!isUnmounted) refreshAll()
    }, 1500)
  } catch (err: any) {
    ElMessage.error(err.message || '触发任务失败')
  } finally {
    runningIds.delete(job.id)
  }
}

const handleDelete = async (job: ScheduledJob) => {
  if (deletingIds.has(job.id)) return
  if (job.is_builtin) {
    ElMessage.warning('系统内置任务不可删除，如不需要可将其停用')
    return
  }
  try {
    await ElMessageBox.confirm(`确定删除定时任务「${job.name}」吗？历史运行日志将一并清理。`, '删除确认', {
      type: 'warning',
      confirmButtonText: '确定删除',
      cancelButtonText: '取消',
      confirmButtonClass: 'el-button--danger'
    })
  } catch {
    return
  }
  deletingIds.add(job.id)
  try {
    await schedulerApi.deleteJob(job.id)
    ElMessage.success(`任务「${job.name}」已删除`)
    await refreshAll()
  } catch (err: any) {
    ElMessage.error(err.message || '删除失败')
  } finally {
    deletingIds.delete(job.id)
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
    case 'wechat_ilink':
      return '微信 iLink 机器人'
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
  isUnmounted = false
  refreshAll()
})

onBeforeUnmount(() => {
  isUnmounted = true
  if (runRefreshTimer) {
    clearTimeout(runRefreshTimer)
    runRefreshTimer = null
  }
})
</script>

<style scoped>
.scheduler-page {
  height: 100%;
  min-height: 0;
  overflow: hidden;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.page-header {
  flex-shrink: 0;
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
  color: #10b981;
}

/* Metric Cards */
.metrics-grid {
  flex-shrink: 0;
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.metric-card {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 14px 18px;
  background: var(--mf-surface-card, var(--el-bg-color));
  border: 1px solid var(--mf-border, var(--el-border-color-lighter));
  border-radius: var(--mf-radius-md, 10px);
  box-shadow: var(--mf-shadow-sm, 0 1px 3px rgba(0, 0, 0, 0.04));
  transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.2s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.2s ease;
}

.metric-card.clickable {
  cursor: pointer;
}

.metric-card.clickable:hover {
  transform: translateY(-2px);
  border-color: var(--mf-border-hover, var(--el-color-primary));
  box-shadow: var(--mf-shadow-glow, 0 4px 16px rgba(59, 130, 246, 0.2));
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
  background: var(--mf-primary-light, rgba(59, 130, 246, 0.12));
  color: var(--mf-primary, #3b82f6);
}

.metric-icon-wrap.success {
  background: var(--mf-success-light, rgba(16, 185, 129, 0.14));
  color: var(--mf-success, #10b981);
}

.metric-icon-wrap.warning {
  background: var(--mf-warning-light, rgba(245, 158, 11, 0.14));
  color: var(--mf-warning, #f59e0b);
}

.metric-icon-wrap.info {
  background: rgba(16, 185, 129, 0.14);
  color: #10b981;
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

.unit-text {
  font-size: 13px;
  font-weight: 400;
  color: var(--el-text-color-secondary);
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
  flex-shrink: 0;
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 14px 18px;
  background: var(--mf-surface-card, var(--el-bg-color));
  border: 1px solid var(--mf-border, var(--el-border-color-lighter));
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
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--mf-surface-card, var(--el-bg-color));
  border: 1px solid var(--mf-border, var(--el-border-color-lighter));
  border-radius: 8px;
  padding: 14px;
}

.table-container :deep(.el-table) {
  flex: 1;
  min-height: 0;
}

.table-container .empty-state {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-primary-light, rgba(59, 130, 246, 0.08));
}

:deep(.premium-header th) {
  background-color: var(--mf-primary-light, rgba(59, 130, 246, 0.08)) !important;
  color: var(--mf-text-muted, var(--el-text-color-secondary)) !important;
  font-weight: 600;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border, var(--el-border-color-lighter)) !important;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border, var(--el-border-color-lighter));
  padding: 14px 0;
}

.jobs-table {
  width: 100%;
}

.job-name-cell {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
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
  background: rgba(139, 92, 246, 0.16);
  color: #a78bfa;
}

[data-theme="light"] .job-icon-wrap.ai_prompt {
  color: #7c3aed;
}

.job-icon-wrap.structured_report {
  background: rgba(59, 130, 246, 0.16);
  color: #60a5fa;
}

[data-theme="light"] .job-icon-wrap.structured_report {
  color: #2563eb;
}

.job-icon-wrap.system_builtin {
  background: rgba(14, 165, 233, 0.16);
  color: #0284c7;
}

.job-type-tag.builtin-badge {
  background: rgba(100, 116, 139, 0.1);
  color: #475569;
  border-color: rgba(100, 116, 139, 0.25);
  font-weight: 500;
  display: inline-flex;
  align-items: center;
  gap: 3px;
}

.badge-icon {
  font-size: 12px;
}

.stats-pills .pill-item.builtin b {
  color: #0284c7;
}

.stats-pills .pill-item.custom b {
  color: #8b5cf6;
}

.job-type-icon {
  font-size: 20px;
}

.job-name-info {
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-width: 0;
}

.job-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.job-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
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
  font-family: var(--font-mono, monospace);
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

.running-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}

.spin-icon {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.duration-hint {
  font-family: var(--font-mono, monospace);
  font-size: 11px;
  color: var(--el-text-color-placeholder);
}

.last-run-time {
  font-family: var(--font-mono, monospace);
  font-size: 11px;
  color: var(--el-text-color-placeholder);
  margin-top: 2px;
}

.next-run-time {
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--el-text-color-regular);
}

.tabular-nums {
  font-variant-numeric: tabular-nums;
}

.text-placeholder {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
}

.row-actions {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: nowrap;
}

.row-actions :deep(.el-button + .el-button) {
  margin-left: 0;
}

.empty-state {
  padding: 40px 0;
}

.table-pagination {
  margin-top: 12px;
  flex-shrink: 0;
  display: flex;
  justify-content: flex-end;
}

/* Responsive Media Queries */
@media (max-width: 1200px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 900px) {
  .toolbar-card {
    flex-direction: column;
    align-items: stretch;
    gap: 14px;
  }
  .search-filter-wrap {
    flex-direction: column;
    align-items: stretch;
  }
  .search-input {
    max-width: 100%;
  }
  .toggle-stats-wrap {
    justify-content: space-between;
  }
}

@media (max-width: 768px) {
  .scheduler-page {
    height: auto;
    min-height: 100%;
    overflow: visible;
    padding: 0 0 16px 0;
  }
  .table-container {
    flex: none;
    overflow: visible;
  }
  .page-header {
    flex-direction: column;
    align-items: stretch;
    gap: 14px;
  }
  .page-title {
    font-size: 18px;
  }
  .page-subtitle {
    display: none;
  }
  .header-actions {
    flex-wrap: wrap;
  }
}

@media (max-width: 640px) {
  .metrics-grid {
    grid-template-columns: 1fr;
  }
}
</style>
