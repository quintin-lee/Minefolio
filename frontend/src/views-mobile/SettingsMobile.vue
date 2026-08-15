<template>
  <div class="settings-mobile">
    <div class="page-header"><h2>我的</h2></div>
    <div class="sync-status">
      <span>待同步：{{ pending }}</span>
      <span>上次同步：{{ lastSync || '从未' }}</span>
      <el-button size="small" :loading="syncing" @click="syncNow">立即同步</el-button>
    </div>
    <el-button @click="exportCsv" block>导出 CSV</el-button>
    <el-button @click="goCategories" block>分类管理</el-button>
    <el-button @click="logout" block>退出登录</el-button>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import http from '@/utils/http'

const router = useRouter()
const sync = useSyncStore()
const auth = useAuthStore()
const pending = computed(() => sync.pendingCount)
const lastSync = computed(() => sync.lastSyncAt)
const syncing = computed(() => sync.syncing)

function syncNow() { sync.syncNow() }
function goCategories() { router.push('/m/settings') }
async function exportCsv() {
  const blob = (await http.get('/export/daily-expenses', { responseType: 'blob' })) as unknown as Blob
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = 'daily_expenses.csv'
  a.click()
  URL.revokeObjectURL(url)
}
function logout() { auth.logout(); router.replace('/m/login') }
</script>

<style scoped>
.sync-status { display: flex; flex-direction: column; gap: 8px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin-bottom: 12px; }
.settings-mobile > * { margin-bottom: 12px; }
</style>
