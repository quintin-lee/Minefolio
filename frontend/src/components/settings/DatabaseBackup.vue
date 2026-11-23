<template>
  <div class="panel-container">
    <div class="panel-header">
      <div class="header-main">
        <h3>{{ t('settings.backupTitle') }}</h3>
        <p class="export-hint">{{ t('settings.backupHint') }}</p>
      </div>
      <div class="header-actions">
        <el-button
          type="primary"
          class="action-btn"
          :loading="creating"
          @click="handleCreateBackup"
        >
          <Icon icon="ph:plus-bold" width="16" style="margin-right: 6px;" />
          {{ backupTask.running.value ? `${t('settings.createBackup')} (${backupTask.progress.value}%)` : t('settings.createBackup') }}
        </el-button>
        <el-button :loading="loading" @click="fetchStatus">
          <Icon icon="ph:arrows-clockwise" width="16" style="margin-right: 6px;" />
          {{ t('common.refresh') || '刷新' }}
        </el-button>
      </div>
    </div>

    <div v-if="backupTask.running.value" style="margin-bottom: 16px;">
      <el-progress :percentage="backupTask.progress.value" :stroke-width="6" />
      <div style="margin-top: 6px; font-size: 12px; color: var(--mf-text-muted)">{{ backupTask.message.value || t('settings.createBackup') }}</div>
    </div>

    <!-- 存储指标卡片 -->
    <div class="metric-grid" v-loading="loading">
      <div class="metric-card">
        <div class="metric-icon-box cyan">
          <Icon icon="ph:database-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.dbDriver') }}</div>
          <div class="metric-val mono">{{ (status?.db_driver || 'sqlite').toUpperCase() }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box purple">
          <Icon icon="ph:hard-drive-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.dbSize') }}</div>
          <div class="metric-val mono">{{ formatBytes(status?.db_size || 0) }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box orange">
          <Icon icon="ph:file-text-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.walSize') }}</div>
          <div class="metric-val mono">{{ formatBytes(status?.wal_size || 0) }}</div>
        </div>
      </div>

      <div class="metric-card">
        <div class="metric-icon-box green">
          <Icon icon="ph:chart-pie-slice-bold" width="20" />
        </div>
        <div class="metric-content">
          <div class="metric-label">{{ t('settings.diskFree') }}</div>
          <div class="metric-val mono">{{ formatBytes(status?.disk_free || 0) }}</div>
        </div>
      </div>
    </div>

    <!-- 备份快照历史表格 -->
    <div class="backup-table-section">
      <div class="section-title-row">
        <h4>{{ t('settings.backupHistory') }}</h4>
        <el-tag size="small" type="info" round>{{ backups.length }}</el-tag>
      </div>

      <el-table
        :data="backups"
        v-loading="loading"
        style="width: 100%; margin-top: 12px;"
        empty-text="暂无历史备份快照"
        class="backup-table"
      >
        <el-table-column prop="filename" :label="t('settings.backupFilename')" min-width="240">
          <template #default="{ row }">
            <div class="file-cell">
              <Icon icon="ph:file-archive-bold" width="18" class="file-icon" />
              <span class="file-name mono">{{ row.filename }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column prop="size" :label="t('settings.backupFileSize')" width="130">
          <template #default="{ row }">
            <span class="mono">{{ formatBytes(row.size) }}</span>
          </template>
        </el-table-column>

        <el-table-column prop="created_at" :label="t('settings.backupTime')" width="180">
          <template #default="{ row }">
            <span class="text-muted">{{ formatTime(row.created_at) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作" width="160" align="right">
          <template #default="{ row }">
            <el-button
              type="primary"
              link
              size="small"
              :loading="downloadingFile === row.filename"
              @click="handleDownload(row.filename)"
            >
              <Icon icon="ph:download-simple-bold" width="14" style="margin-right: 4px;" />
              {{ t('settings.download') }}
            </el-button>

            <el-popconfirm
              :title="t('settings.deleteBackupConfirm', { name: row.filename })"
              confirm-button-type="danger"
              @confirm="handleDelete(row.filename)"
            >
              <template #reference>
                <el-button type="danger" link size="small">
                  <Icon icon="ph:trash-bold" width="14" style="margin-right: 4px;" />
                  {{ t('common.delete') || '删除' }}
                </el-button>
              </template>
            </el-popconfirm>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { t } from '@/utils/locale'
import { backupApi, type BackupStatus, type BackupItem } from '@/api/backup'
import { useAsyncTaskProgress } from '@/composables/useAsyncTaskProgress'

const loading = ref(false)
const creating = ref(false)
const downloadingFile = ref<string | null>(null)
const status = ref<BackupStatus | null>(null)

const backups = computed<BackupItem[]>(() => {
  return status.value?.backups || []
})

function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

function formatTime(ts: number): string {
  if (!ts) return '-'
  const date = new Date(ts > 1e11 ? ts : ts * 1000)
  return date.toLocaleString()
}

async function fetchStatus() {
  loading.value = true
  try {
    status.value = await backupApi.getStatus()
  } catch (err) {
    console.error('Failed to load backup status:', err)
  } finally {
    loading.value = false
  }
}

const backupTask = useAsyncTaskProgress()

async function handleCreateBackup() {
  creating.value = true
  try {
    await backupTask.run((opts) => backupApi.createBackup(opts))
    ElMessage.success(t('settings.createBackupSuccess'))
    await fetchStatus()
  } catch (err) {
    if (backupTask.isCancelledError(err)) return
    ElMessage.error(t('settings.createBackupFailed'))
  } finally {
    creating.value = false
  }
}

async function handleDownload(filename: string) {
  downloadingFile.value = filename
  try {
    const blob = await backupApi.downloadBackup(filename)
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = filename
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
  } catch (err) {
    ElMessage.error(t('settings.downloadFailed'))
  } finally {
    downloadingFile.value = null
  }
}

async function handleDelete(filename: string) {
  try {
    await backupApi.deleteBackup(filename)
    ElMessage.success(t('settings.deleteBackupSuccess'))
    await fetchStatus()
  } catch (err) {
    ElMessage.error(t('settings.deleteBackupFailed'))
  }
}

onMounted(() => {
  fetchStatus()
})
</script>

<style scoped>
.panel-container {
  padding: 24px;
}

.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin-bottom: 20px;
}

.header-main h3 {
  margin: 0 0 6px 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.export-hint {
  margin: 0;
  font-size: 13px;
  color: var(--mf-text-muted);
  line-height: 1.5;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
}

.metric-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
  margin-bottom: 24px;
}

.metric-card {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px;
  background: var(--mf-card-bg, #ffffff);
  border: 1px solid var(--mf-border, #e5e7eb);
  border-radius: 12px;
  transition: all 0.2s ease;
}

.metric-card:hover {
  border-color: var(--el-color-primary-light-5);
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.04);
}

.metric-icon-box {
  width: 42px;
  height: 42px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.metric-icon-box.cyan {
  background: rgba(6, 182, 212, 0.12);
  color: #0891b2;
}

.metric-icon-box.purple {
  background: rgba(147, 51, 234, 0.12);
  color: #9333ea;
}

.metric-icon-box.orange {
  background: rgba(249, 115, 22, 0.12);
  color: #ea580c;
}

.metric-icon-box.green {
  background: rgba(34, 197, 94, 0.12);
  color: #16a34a;
}

.metric-content {
  overflow: hidden;
}

.metric-label {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-bottom: 4px;
}

.metric-val {
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.mono {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

.backup-table-section {
  background: var(--mf-card-bg, #ffffff);
  border: 1px solid var(--mf-border, #e5e7eb);
  border-radius: 12px;
  padding: 18px 20px;
}

.section-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.section-title-row h4 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.file-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.file-icon {
  color: var(--el-color-primary);
  flex-shrink: 0;
}

.file-name {
  font-size: 13px;
  color: var(--mf-text-main);
}

.text-muted {
  font-size: 13px;
  color: var(--mf-text-muted);
}
</style>
