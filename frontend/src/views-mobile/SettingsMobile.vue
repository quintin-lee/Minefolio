<template>
  <div class="settings-mobile">
    <div class="page-header"><h2>我的</h2></div>

    <div class="theme-card">
      <div class="card-title">外观主题</div>
      <p class="card-hint">选择深色 / 浅色 / 跟随系统</p>
      <div class="theme-options" role="radiogroup" aria-label="外观主题">
        <button
          v-for="opt in themeOptions"
          :key="opt.value"
          type="button"
          class="theme-option"
          :class="{ active: theme.mode === opt.value }"
          role="radio"
          :aria-checked="theme.mode === opt.value"
          @click="theme.setMode(opt.value)"
        >
          <el-icon :size="18" class="opt-icon"><component :is="opt.icon" /></el-icon>
          <span class="opt-label">{{ opt.label }}</span>
          <el-icon v-if="theme.mode === opt.value" :size="15" class="opt-check"><Select /></el-icon>
        </button>
      </div>
      <div class="theme-current">当前主题：{{ resolvedLabel }}</div>
    </div>

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
import { Monitor, Moon, Select, Sunny } from '@element-plus/icons-vue'
import { useThemeStore, type ThemeMode } from '@/stores/theme'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import http from '@/utils/http'

const router = useRouter()
const sync = useSyncStore()
const auth = useAuthStore()
const theme = useThemeStore()

const themeOptions: { value: ThemeMode; label: string; icon: typeof Moon }[] = [
  { value: 'dark', label: '深色模式', icon: Moon },
  { value: 'light', label: '浅色模式', icon: Sunny },
  { value: 'auto', label: '跟随系统', icon: Monitor },
]
const resolvedLabel = computed(() => (theme.resolvedTheme === 'dark' ? '深色' : '浅色'))

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

.theme-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 16px;
}
.card-title { font-size: 15px; font-weight: 600; color: var(--mf-text-main); }
.card-hint { font-size: 12px; color: var(--mf-text-muted); margin: 4px 0 12px; }
.theme-options {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.theme-option {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 12px 14px;
  border-radius: 10px;
  border: 1px solid var(--mf-border);
  background: var(--mf-surface-muted);
  color: var(--mf-text-regular);
  font-size: 14px;
  cursor: pointer;
  transition: var(--mf-transition);
  text-align: left;
}
.theme-option.active {
  border-color: var(--mf-primary);
  background: var(--mf-primary-light);
  color: var(--mf-primary);
}
.opt-label { flex: 1; }
.opt-check { color: var(--mf-primary); }
.theme-current {
  margin-top: 12px;
  font-size: 12px;
  color: var(--mf-text-muted);
}
</style>
