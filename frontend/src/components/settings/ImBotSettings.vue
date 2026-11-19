<template>
  <div class="panel-container">
    <div class="panel-header">
      <div class="header-main">
        <h3>{{ t('settings.imBotTitle') }}</h3>
        <p class="export-hint">{{ t('settings.imBotDesc') }}</p>
      </div>
      <div class="header-actions">
        <el-button
          type="primary"
          class="action-btn"
          :loading="generating"
          @click="handleGenerateToken"
        >
          <Icon icon="ph:key-bold" width="16" style="margin-right: 6px;" />
          {{ t('settings.generateBindToken') }}
        </el-button>
        <el-button :loading="loading" @click="fetchBindings">
          <Icon icon="ph:arrows-clockwise" width="16" style="margin-right: 6px;" />
          {{ t('common.refresh') || '刷新' }}
        </el-button>
      </div>
    </div>

    <!-- 绑定码展示区域 -->
    <transition name="el-fade-in">
      <div v-if="bindToken" class="bind-token-card">
        <div class="token-header">
          <div class="token-title">
            <Icon icon="ph:shield-check-bold" width="20" class="token-shield-icon" />
            <span>{{ t('settings.bindTokenTitle') }}</span>
          </div>
          <el-tag :type="remainingSeconds > 60 ? 'warning' : 'danger'" effect="light" round>
            <Icon icon="ph:timer-bold" width="14" style="margin-right: 4px; vertical-align: -2px;" />
            {{ formattedRemainingTime }}
          </el-tag>
        </div>

        <p class="token-hint">{{ t('settings.bindTokenHint') }}</p>

        <div class="token-code-row">
          <code class="bind-command mono">/bind {{ bindToken }}</code>
          <el-button type="primary" size="small" @click="copyBindCommand">
            <Icon icon="ph:copy" width="14" style="margin-right: 4px;" />
            {{ t('settings.copyCommand') }}
          </el-button>
        </div>

        <div class="token-extra-tip">
          <Icon icon="ph:info" width="14" />
          <span>{{ t('settings.bindDirectHint') }} <code class="inline-code mono">/bind &lt;用户名&gt; &lt;密码&gt;</code></span>
        </div>
      </div>
    </transition>

    <!-- 已绑定账号表格 -->
    <div class="bindings-table-section">
      <div class="section-title-row">
        <h4>{{ t('settings.boundAccounts') }}</h4>
        <el-tag size="small" type="info" round>{{ bindings.length }}</el-tag>
      </div>

      <el-table
        :data="bindings"
        v-loading="loading"
        style="width: 100%; margin-top: 12px;"
        :empty-text="t('settings.noBindings')"
        class="bindings-table"
      >
        <el-table-column prop="platform" :label="t('settings.imPlatform')" width="140">
          <template #default="{ row }">
            <div class="platform-badge">
              <Icon
                :icon="row.platform === 'qq' ? 'ri:qq-fill' : 'ri:wechat-fill'"
                width="18"
                :class="row.platform"
              />
              <span class="platform-name">{{ row.platform === 'qq' ? 'QQ' : row.platform === 'wechat' ? '微信' : row.platform }}</span>
            </div>
          </template>
        </el-table-column>

        <el-table-column prop="im_user_id" :label="t('settings.imUserId')" min-width="200">
          <template #default="{ row }">
            <span class="mono user-id-text">{{ row.im_user_id }}</span>
          </template>
        </el-table-column>

        <el-table-column prop="scope" :label="t('settings.imScope')" width="160">
          <template #default="{ row }">
            <el-tag size="small" type="success" effect="plain">
              <Icon icon="ph:lock-key-bold" width="12" style="margin-right: 4px;" />
              {{ row.scope === 'readonly' ? t('settings.imScopeReadonly') : row.scope }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column prop="created_at" :label="t('settings.imCreatedAt')" width="180">
          <template #default="{ row }">
            <span class="text-muted">{{ formatTime(row.created_at) }}</span>
          </template>
        </el-table-column>

        <el-table-column :label="t('common.actions') || '操作'" width="120" align="right">
          <template #default="{ row }">
            <el-popconfirm
              :title="t('settings.unbindConfirm', { platform: row.platform.toUpperCase(), id: row.im_user_id })"
              confirm-button-type="danger"
              @confirm="handleUnbind(row.platform, row.im_user_id)"
            >
              <template #reference>
                <el-button type="danger" link size="small" :loading="unbindingKey === `${row.platform}:${row.im_user_id}`">
                  <Icon icon="ph:link-break-bold" width="14" style="margin-right: 4px;" />
                  {{ t('settings.unbind') }}
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
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { t } from '@/utils/locale'
import { imApi, type ImBindingItem } from '@/api/im'

const loading = ref(false)
const generating = ref(false)
const bindings = ref<ImBindingItem[]>([])
const bindToken = ref<string | null>(null)
const remainingSeconds = ref<number>(0)
const unbindingKey = ref<string | null>(null)
let timerId: any = null

const formattedRemainingTime = computed(() => {
  const m = Math.floor(remainingSeconds.value / 60)
  const s = remainingSeconds.value % 60
  return `${m}:${s < 10 ? '0' : ''}${s}`
})

function formatTime(val: string): string {
  if (!val) return '-'
  try {
    return new Date(val).toLocaleString()
  } catch {
    return val
  }
}

async function fetchBindings() {
  loading.value = true
  try {
    bindings.value = await imApi.getBindings()
  } catch (err) {
    console.error('Failed to load IM bindings:', err)
  } finally {
    loading.value = false
  }
}

async function handleGenerateToken() {
  generating.value = true
  try {
    const res = await imApi.generateBindToken()
    bindToken.value = res.bind_token
    remainingSeconds.value = res.ttl_sec || 300

    if (timerId) clearInterval(timerId)
    timerId = setInterval(() => {
      if (remainingSeconds.value > 0) {
        remainingSeconds.value--
      } else {
        bindToken.value = null
        clearInterval(timerId)
        timerId = null
      }
    }, 1000)
  } catch (err) {
    ElMessage.error(t('settings.generateTokenFailed') || '生成绑定码失败')
  } finally {
    generating.value = false
  }
}

async function copyBindCommand() {
  if (!bindToken.value) return
  const cmd = `/bind ${bindToken.value}`
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(cmd)
    } else {
      const ta = document.createElement('textarea')
      ta.value = cmd
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
    }
    ElMessage.success(t('settings.commandCopied'))
  } catch {
    ElMessage.info(cmd)
  }
}

async function handleUnbind(platform: string, imUserId: string) {
  unbindingKey.value = `${platform}:${imUserId}`
  try {
    await imApi.unbind(platform, imUserId)
    ElMessage.success(t('settings.unbindSuccess'))
    await fetchBindings()
  } catch (err) {
    ElMessage.error(t('settings.unbindFailed'))
  } finally {
    unbindingKey.value = null
  }
}

onMounted(() => {
  fetchBindings()
})

onUnmounted(() => {
  if (timerId) clearInterval(timerId)
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

.bind-token-card {
  background: var(--el-color-primary-light-9);
  border: 1px solid var(--el-color-primary-light-7);
  border-radius: 12px;
  padding: 16px 20px;
  margin-bottom: 24px;
}

.token-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.token-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  font-weight: 600;
  color: var(--el-color-primary-dark-2);
}

.token-shield-icon {
  color: var(--el-color-primary);
}

.token-hint {
  margin: 0 0 12px 0;
  font-size: 13px;
  color: var(--mf-text-main);
}

.token-code-row {
  display: flex;
  align-items: center;
  gap: 12px;
  background: var(--mf-card-bg, #ffffff);
  border: 1px solid var(--mf-border, #e5e7eb);
  border-radius: 8px;
  padding: 8px 12px;
  margin-bottom: 10px;
}

.bind-command {
  flex: 1;
  font-size: 13px;
  color: var(--el-color-primary);
  word-break: break-all;
}

.token-extra-tip {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--mf-text-muted);
}

.inline-code {
  background: rgba(0, 0, 0, 0.05);
  padding: 2px 6px;
  border-radius: 4px;
  color: var(--mf-text-main);
}

.bindings-table-section {
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

.platform-badge {
  display: flex;
  align-items: center;
  gap: 8px;
}

.platform-badge .qq {
  color: #12b7f5;
}

.platform-badge .wechat {
  color: #07c160;
}

.platform-name {
  font-size: 13px;
  font-weight: 500;
}

.user-id-text {
  font-size: 13px;
  color: var(--mf-text-main);
}

.mono {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}

.text-muted {
  font-size: 13px;
  color: var(--mf-text-muted);
}
</style>
