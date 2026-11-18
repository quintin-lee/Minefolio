<template>
  <el-drawer
    v-model="visible"
    :title="`任务执行记录 - ${jobName || '全部'}`"
    :size="drawerSize"
    destroy-on-close
  >
    <div class="drawer-container">
      <!-- Toolbar -->
      <div class="log-toolbar">
        <div class="toolbar-left">
          <span class="total-badge">共 {{ total }} 次历史执行记录</span>
        </div>
        <el-button
          plain
          size="small"
          :loading="loading"
          @click="fetchLogs"
        >
          <Icon icon="ph:arrows-clockwise" class="btn-icon" />
          刷新
        </el-button>
      </div>

      <!-- Logs Table -->
      <el-table
        v-loading="loading"
        :data="logs"
        class="premium-table logs-table"
        row-class-name="premium-row"
        header-cell-class-name="premium-header"
        empty-text="暂无历史执行记录"
      >
        <el-table-column type="expand">
          <template #default="{ row }">
            <div class="expanded-detail">
              <div v-if="row.error_message" class="error-box">
                <div class="box-title">
                  <Icon icon="ph:warning-circle-bold" class="error-icon" />
                  异常报错堆栈
                </div>
                <pre class="error-pre">{{ row.error_message }}</pre>
              </div>

              <div v-if="row.output_summary" class="summary-box">
                <div class="box-title">
                  <Icon icon="ph:file-text-bold" class="summary-icon" />
                  生成简报 Markdown 内容 (已推送微信)
                </div>
                <div class="markdown-preview" v-html="renderMarkdown(row.output_summary)" />
              </div>

              <div v-if="!row.error_message && !row.output_summary" class="empty-detail">
                暂无额外输出信息
              </div>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="执行时间" min-width="155">
          <template #default="{ row }">
            <div class="time-cell">
              <span class="time-text tabular-nums">{{ row.started_at }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column label="触发原因" prop="trigger_reason" min-width="120" show-overflow-tooltip>
          <template #default="{ row }">
            <el-tag size="small" type="info" effect="plain">
              {{ formatTriggerReason(row.trigger_reason) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="耗时" width="90">
          <template #default="{ row }">
            <span class="duration-text tabular-nums">{{ formatDuration(row.duration_ms) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="执行状态" width="95">
          <template #default="{ row }">
            <el-tag
              size="small"
              :type="getStatusType(row.status)"
              effect="light"
            >
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="推送状态" width="95">
          <template #default="{ row }">
            <el-tag
              size="small"
              :type="getNotifyStatusType(row.notification_status)"
              effect="light"
            >
              {{ getNotifyStatusLabel(row.notification_status) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="操作" width="70" fixed="right">
          <template #default="{ row }">
            <el-button
              type="primary"
              link
              size="small"
              @click="previewLog(row as any)"
            >
              详情
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- Pagination -->
      <div class="pagination-footer">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          :page-sizes="[10, 20, 50]"
          layout="total, prev, pager, next"
          background
          size="small"
          @size-change="fetchLogs"
          @current-change="fetchLogs"
        />
      </div>
    </div>

    <!-- Preview Modal -->
    <el-dialog
      v-model="previewVisible"
      title="执行输出详情"
      :width="previewWidth"
      append-to-body
      destroy-on-close
    >
      <div v-if="selectedLog" class="preview-dialog-content">
        <div class="meta-banner">
          <span class="tabular-nums"><b>触发时间:</b> {{ selectedLog.started_at }}</span>
          <span class="tabular-nums"><b>耗时:</b> {{ formatDuration(selectedLog.duration_ms) }}</span>
          <span><b>执行状态:</b> {{ getStatusLabel(selectedLog.status) }}</span>
          <span><b>推送状态:</b> {{ getNotifyStatusLabel(selectedLog.notification_status) }}</span>
        </div>

        <div v-if="selectedLog.error_message" class="error-box">
          <div class="box-title">
            <Icon icon="ph:warning-circle-bold" class="error-icon" />
            错误详情
          </div>
          <pre class="error-pre">{{ selectedLog.error_message }}</pre>
        </div>

        <div v-if="selectedLog.output_summary" class="summary-box">
          <div class="box-title">
            <Icon icon="ph:file-text-bold" class="summary-icon" />
            生成的研报内容
          </div>
          <div class="markdown-preview" v-html="renderMarkdown(selectedLog.output_summary)" />
        </div>
      </div>
    </el-dialog>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import { schedulerApi } from '@/api/scheduler'
import type { JobRunLog, ScheduledJob } from '@/types'

const visible = ref(false)
const loading = ref(false)
const currentJobId = ref<number>(0)
const jobName = ref('')
const logs = ref<JobRunLog[]>([])
const total = ref(0)
const page = ref(1)
const pageSize = ref(20)

const previewVisible = ref(false)
const selectedLog = ref<JobRunLog | null>(null)
const windowWidth = ref(typeof window !== 'undefined' ? window.innerWidth : 1024)

const updateWindowWidth = () => {
  windowWidth.value = window.innerWidth
}

onMounted(() => {
  window.addEventListener('resize', updateWindowWidth)
})

onUnmounted(() => {
  window.removeEventListener('resize', updateWindowWidth)
})

const drawerSize = computed(() => (windowWidth.value < 768 ? '100%' : '720px'))
const previewWidth = computed(() => (windowWidth.value < 768 ? '94vw' : '640px'))

const open = (job?: ScheduledJob) => {
  visible.value = true
  if (job) {
    currentJobId.value = job.id
    jobName.value = job.name
  } else {
    currentJobId.value = 0
    jobName.value = ''
  }
  page.value = 1
  fetchLogs()
}

const fetchLogs = async () => {
  loading.value = true
  try {
    const res = await schedulerApi.listLogs(currentJobId.value, page.value, pageSize.value)
    logs.value = res.items || []
    total.value = res.total || 0
  } catch (err: any) {
    ElMessage.error(err.message || '获取执行日志失败')
  } finally {
    loading.value = false
  }
}

const previewLog = (row: JobRunLog) => {
  selectedLog.value = row
  previewVisible.value = true
}

const formatTriggerReason = (reason: string) => {
  if (!reason) return '手动执行'
  if (reason.startsWith('cron:')) return `定时 ${reason.slice(5).trim()}`
  if (reason.startsWith('event:')) return `事件 ${reason.slice(6).trim()}`
  if (reason === 'manual_run') return '手动触发'
  return reason
}

const formatDuration = (ms: number) => {
  if (!ms || ms <= 0) return '0ms'
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}

const getStatusType = (status: string) => {
  switch (status) {
    case 'success':
      return 'success'
    case 'failed':
      return 'danger'
    case 'running':
      return 'primary'
    case 'timeout':
      return 'warning'
    default:
      return 'info'
  }
}

const getStatusLabel = (status: string) => {
  switch (status) {
    case 'success':
      return '成功'
    case 'failed':
      return '失败'
    case 'running':
      return '执行中'
    case 'timeout':
      return '超时'
    default:
      return status
  }
}

const getNotifyStatusType = (status?: string) => {
  switch (status) {
    case 'sent':
      return 'success'
    case 'failed':
      return 'danger'
    case 'none':
    default:
      return 'info'
  }
}

const getNotifyStatusLabel = (status?: string) => {
  switch (status) {
    case 'sent':
      return '已推微信'
    case 'failed':
      return '推送失败'
    case 'none':
    default:
      return '未推送'
  }
}

const renderMarkdown = (text?: string) => {
  if (!text) return ''
  try {
    const raw = marked.parse(text, { async: false, breaks: true }) as string
    return DOMPurify.sanitize(raw)
  } catch {
    return DOMPurify.sanitize(text.replace(/</g, '&lt;').replace(/>/g, '&gt;'))
  }
}

defineExpose({
  open
})
</script>

<style scoped>
.drawer-container {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.log-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.total-badge {
  font-size: 13px;
  color: var(--el-text-color-secondary);
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
  padding: 10px 0;
  border-bottom: 1px solid var(--mf-border, var(--el-border-color-lighter)) !important;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border, var(--el-border-color-lighter));
  padding: 12px 0;
}

.logs-table {
  flex: 1;
}

.time-cell {
  font-size: 12px;
  color: var(--el-text-color-regular);
}

.duration-text {
  font-family: var(--font-mono, monospace);
  font-size: 12px;
}

.tabular-nums {
  font-variant-numeric: tabular-nums;
  font-family: var(--font-mono, monospace);
}

.pagination-footer {
  margin-top: 16px;
  display: flex;
  justify-content: flex-end;
}

.expanded-detail {
  padding: 12px 16px;
  background: var(--el-fill-color-lighter);
  border-radius: 6px;
}

.error-box {
  margin-bottom: 12px;
  padding: 10px 14px;
  background: var(--el-color-danger-light-9);
  border-left: 3px solid var(--el-color-danger);
  border-radius: 4px;
}

.box-title {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  font-weight: 600;
  margin-bottom: 8px;
}

.error-icon {
  color: var(--el-color-danger);
}

.summary-icon {
  color: var(--el-color-primary);
}

.error-pre {
  margin: 0;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-all;
  color: var(--el-color-danger);
}

.summary-box {
  padding: 12px 14px;
  background: var(--mf-surface-card, var(--el-bg-color));
  border: 1px solid var(--mf-border, var(--el-border-color-lighter));
  border-radius: 6px;
}

.markdown-preview {
  font-size: 13px;
  line-height: 1.6;
  color: var(--el-text-color-primary);
}

.markdown-preview :deep(h1),
.markdown-preview :deep(h2),
.markdown-preview :deep(h3) {
  margin: 12px 0 6px;
  color: var(--el-text-color-primary);
}

.markdown-preview :deep(p) {
  margin: 6px 0;
}

.markdown-preview :deep(ul),
.markdown-preview :deep(ol) {
  padding-left: 20px;
  margin: 6px 0;
}

.markdown-preview :deep(code) {
  font-family: var(--font-mono, monospace);
  background: var(--el-fill-color-light);
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 12px;
}

.markdown-preview :deep(pre) {
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid var(--mf-border, var(--el-border-color-lighter));
  padding: 12px;
  border-radius: 6px;
  overflow-x: auto;
}

.markdown-preview :deep(pre code) {
  background: transparent;
  padding: 0;
}

.markdown-preview :deep(blockquote) {
  border-left: 3px solid var(--el-color-primary);
  margin: 8px 0;
  padding: 6px 12px;
  color: var(--el-text-color-secondary);
  background: var(--mf-primary-light, rgba(59, 130, 246, 0.06));
  border-radius: 0 4px 4px 0;
}

.markdown-preview :deep(table) {
  border-collapse: collapse;
  width: 100%;
  margin: 8px 0;
}

.markdown-preview :deep(th),
.markdown-preview :deep(td) {
  border: 1px solid var(--mf-border, var(--el-border-color));
  padding: 6px 10px;
}

.markdown-preview :deep(th) {
  background: var(--mf-primary-light, rgba(59, 130, 246, 0.08));
}

.empty-detail {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
  text-align: center;
  padding: 8px 0;
}

.preview-dialog-content {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.meta-banner {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  font-size: 12px;
  padding: 10px 14px;
  background: var(--el-fill-color-light);
  border-radius: 6px;
  border: 1px solid var(--el-border-color-lighter);
}
</style>
