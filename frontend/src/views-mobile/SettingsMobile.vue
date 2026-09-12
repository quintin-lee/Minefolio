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

    <!-- 服务端地址设置卡片 -->
    <div class="server-card">
      <div class="server-card-header">
        <div>
          <div class="card-title">服务端地址</div>
          <p class="card-hint">配置 Minefolio 后端服务接口地址</p>
        </div>
        <el-tag :type="isCustom ? 'warning' : 'info'" size="small">
          {{ isCustom ? '自定义' : '默认' }}
        </el-tag>
      </div>

      <div class="server-input-row">
        <el-input
          v-model="inputUrl"
          placeholder="例如 http://192.168.1.100:8080"
          clearable
          :disabled="testing || saving"
        >
          <template #prefix>
            <el-icon><Connection /></el-icon>
          </template>
        </el-input>
      </div>

      <div class="server-tips">
        <span>支持输入局域网 IP 或公网域名；留空表示使用默认地址。</span>
      </div>

      <div
        v-if="testResult"
        class="test-result-banner"
        :class="{ 'is-ok': testResult.ok, 'is-err': !testResult.ok }"
      >
        <el-icon class="test-banner-icon">
          <component :is="testResult.ok ? SuccessFilled : CircleCloseFilled" />
        </el-icon>
        <span class="test-banner-msg">{{ testResult.message }}</span>
      </div>

      <div class="server-actions">
        <el-button size="small" :loading="testing" @click="handleTest">
          测试连接
        </el-button>
        <el-button
          v-if="isCustom || inputUrl !== serverUrl"
          size="small"
          type="info"
          plain
          :disabled="testing || saving"
          @click="handleReset"
        >
          恢复默认
        </el-button>
        <el-button
          type="primary"
          size="small"
          :loading="saving"
          :disabled="testing"
          @click="handleSave"
        >
          保存设置
        </el-button>
      </div>
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
import { ref, computed, watch } from 'vue'
import { useRouter } from 'vue-router'
import { Monitor, Moon, Select, Sunny, Connection, SuccessFilled, CircleCloseFilled } from '@element-plus/icons-vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useThemeStore, type ThemeMode } from '@/stores/theme'
import { useSyncStore } from '@/stores/sync'
import { useAuthStore } from '@/stores/auth'
import { dailyExpensesApi } from '@/api/daily_expenses'
import { useServerUrl, type ServerConnectionTestResult } from '@/composables/useServerUrl'

const router = useRouter()
const sync = useSyncStore()
const auth = useAuthStore()
const theme = useThemeStore()
const { serverUrl, isCustom, setUrl, resetUrl, testConnection, isValidServerUrl, normalizeServerUrl } = useServerUrl()

const inputUrl = ref(serverUrl.value)
const testing = ref(false)
const saving = ref(false)
const testResult = ref<ServerConnectionTestResult | null>(null)

watch(serverUrl, (val) => {
  inputUrl.value = val
})

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

async function handleTest() {
  const target = inputUrl.value.trim()
  if (target && !isValidServerUrl(target)) {
    ElMessage.warning('请输入有效的 URL 地址')
    return
  }

  testing.value = true
  testResult.value = null
  try {
    const res = await testConnection(target)
    testResult.value = res
    if (res.ok) {
      ElMessage.success(res.message)
    } else {
      ElMessage.error(res.message)
    }
  } finally {
    testing.value = false
  }
}

async function handleSave() {
  const target = inputUrl.value.trim()
  if (target && !isValidServerUrl(target)) {
    ElMessage.warning('请输入有效的 URL 地址 (如 http://192.168.1.100:8080)')
    return
  }

  if (sync.pendingCount > 0) {
    try {
      await ElMessageBox.confirm(
        `当前有 ${sync.pendingCount} 条离线数据待同步，更换服务端地址可能会将离线数据同步到不同实例。是否继续保存？`,
        '提示',
        { confirmButtonText: '继续保存', cancelButtonText: '取消', type: 'warning' }
      )
    } catch {
      return
    }
  }

  saving.value = true
  try {
    const normalized = normalizeServerUrl(target)
    setUrl(normalized)
    inputUrl.value = normalized
    ElMessage.success('服务端地址已保存并应用')
  } finally {
    saving.value = false
  }
}

function handleReset() {
  resetUrl()
  inputUrl.value = ''
  testResult.value = null
  ElMessage.info('已恢复为默认服务端地址')
}

async function exportCsv() {
  try {
    const blob = await dailyExpensesApi.exportCsv()
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'daily_expenses.csv'
    a.click()
    URL.revokeObjectURL(url)
  } catch {
    // ignore
  }
}
function logout() { auth.logout(); router.replace('/m/login') }
</script>

<style scoped>
.sync-status { display: flex; flex-direction: column; gap: 8px; background: var(--mf-surface); border: 1px solid var(--mf-border); border-radius: 12px; padding: 16px; margin-bottom: 12px; }
.settings-mobile > * { margin-bottom: 12px; }

.server-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: 12px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.server-card-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
}
.server-tips {
  font-size: 11px;
  color: var(--mf-text-muted);
}
.test-result-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 13px;
}
.test-result-banner.is-ok {
  background: var(--mf-success-light);
  border: 1px solid var(--mf-success-border);
  color: var(--mf-success);
}
.test-result-banner.is-err {
  background: var(--mf-danger-light);
  border: 1px solid var(--mf-danger-border);
  color: var(--mf-danger);
}
.test-banner-icon {
  font-size: 16px;
  flex-shrink: 0;
}
.test-banner-msg {
  word-break: break-all;
}
.server-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 4px;
}

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
