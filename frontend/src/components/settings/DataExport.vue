<template>
  <div class="panel-container">
    <div class="panel-header">
      <h3>{{ t('settings.exportData') }}</h3>
    </div>
    <p class="export-hint">{{ t('settings.exportHint') }}</p>
    <el-button
      type="primary"
      class="action-btn"
      @click="handleExport"
      :loading="exporting"
    >
      {{ t('settings.exportButton') }}
    </el-button>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { transactionsApi } from '@/api/transactions'
import { t } from '@/utils/locale'

const exporting = ref(false)

async function handleExport() {
  exporting.value = true
  try {
    const blob = await transactionsApi.exportCsv()
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `minefolio_transactions_${new Date().toISOString().slice(0, 10)}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
    ElMessage.success('导出成功')
  } catch {
    ElMessage.error('导出失败')
  } finally {
    exporting.value = false
  }
}
</script>
