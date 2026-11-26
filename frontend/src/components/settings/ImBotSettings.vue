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
        <el-button :loading="loading" @click="refreshAll">
          <Icon icon="ph:arrows-clockwise" width="16" style="margin-right: 6px;" />
          {{ t('common.refresh') || '刷新' }}
        </el-button>
      </div>
    </div>

    <!-- 微信 iLink 独立实例 -->
    <div class="ilink-section">
      <div class="ilink-header">
        <div class="ilink-title-row">
          <Icon icon="ri:wechat-fill" width="20" class="ilink-wechat-icon" />
          <h4>{{ t('settings.ilinkSectionTitle') }}</h4>
          <el-tag v-if="instance" size="small" :type="statusTagType(instance.status)" effect="light" round>
            {{ statusLabel(instance.status) }}
          </el-tag>
        </div>
        <p class="export-hint">{{ t('settings.ilinkSectionDesc') }}</p>
      </div>

      <el-alert
        v-if="gatewayNotConfigured"
        type="warning"
        :closable="false"
        show-icon
        style="margin-bottom: 14px;"
        :title="t('settings.ilinkGatewayNotConfigured')"
      />

      <div v-if="!instance" class="ilink-empty">
        <div class="ilink-notice">
          <Icon icon="ph:info" width="16" />
          <span>{{ t('settings.ilinkDedicatedNotice') }}</span>
        </div>
        <el-button type="success" :loading="instanceBusy" @click="handleCreateInstance">
          <Icon icon="ph:plus-bold" width="16" style="margin-right: 6px;" />
          {{ t('settings.ilinkCreateBtn') }}
        </el-button>
      </div>

      <div v-else class="ilink-card">
        <!-- 扫码/登录阶段 -->
        <div v-if="isLoginStage(instance.status)" class="ilink-login-box">
          <div v-if="qrDataUrl" class="ilink-qr-wrap">
            <img :src="qrDataUrl" alt="WeChat iLink QR" class="ilink-qr-img" />
          </div>
          <div v-else class="ilink-qr-placeholder">
            <Icon icon="ph:qr-code" width="44" />
            <span>{{ t('settings.ilinkQrWaiting') }}</span>
          </div>

          <div class="ilink-login-meta">
            <p class="ilink-stage-text">{{ statusLabel(instance.status) }}</p>

            <div v-if="instance.status === 'need_verifycode'" class="ilink-verify-row">
              <el-input
                v-model="verifyCodeInput"
                maxlength="8"
                :placeholder="t('settings.ilinkVerifyPlaceholder')"
                style="max-width: 220px;"
                @keyup.enter="handleSubmitVerifyCode"
              />
              <el-button type="primary" :loading="instanceBusy" @click="handleSubmitVerifyCode">
                {{ t('settings.ilinkSubmitVerify') }}
              </el-button>
            </div>
          </div>
        </div>

        <!-- 异常信息 -->
        <el-alert
          v-if="instance.last_error"
          type="error"
          :closable="false"
          show-icon
          style="margin-bottom: 12px;"
          :title="instance.last_error"
        />

        <!-- 运行信息 -->
        <div v-if="instance.ilink_bot_id || instance.im_user_id || instance.last_seen_at" class="ilink-meta-grid">
          <div v-if="instance.ilink_bot_id" class="ilink-meta-item">
            <span class="meta-label">{{ t('settings.ilinkBotId') }}</span>
            <span class="mono">{{ instance.ilink_bot_id }}</span>
          </div>
          <div v-if="instance.im_user_id" class="ilink-meta-item">
            <span class="meta-label">{{ t('settings.ilinkOwnerWechat') }}</span>
            <span class="mono">{{ instance.im_user_id }}</span>
          </div>
          <div v-if="instance.last_seen_at" class="ilink-meta-item">
            <span class="meta-label">{{ t('settings.ilinkLastSeen') }}</span>
            <span>{{ formatTime(instance.last_seen_at) }}</span>
          </div>
        </div>

        <!-- 记账能力开关（仅运行中的实例可切换） -->
        <div v-if="instance.status === 'active'" class="ilink-capability-row">
          <div class="ilink-capability-info">
            <span class="meta-label">{{ t('settings.ilinkCapability') }}</span>
            <span class="ilink-capability-value">
              {{ bookkeepingEnabled ? t('settings.ilinkCapabilityConfirm') : t('settings.ilinkCapabilityReadonly') }}
            </span>
            <div class="ilink-capability-hint">{{ t('settings.ilinkCapabilityHint') }}</div>
          </div>
          <el-popconfirm
            v-if="!bookkeepingEnabled"
            :title="t('settings.ilinkCapabilityEnableConfirm')"
            :confirm-button-text="t('settings.ilinkCapabilityEnableBtn')"
            @confirm="handleCapabilityChange(true)"
          >
            <template #reference>
              <el-button size="small" type="primary" plain :loading="instanceBusy">
                {{ t('settings.ilinkCapabilityEnableBtn') }}
              </el-button>
            </template>
          </el-popconfirm>
          <el-button
            v-else
            size="small"
            plain
            :loading="instanceBusy"
            @click="handleCapabilityChange(false)"
          >
            {{ t('settings.ilinkCapabilityDisableBtn') }}
          </el-button>
        </div>

        <!-- 操作按钮 -->
        <div class="ilink-actions">
          <el-button
            v-if="instance.status !== 'disabled'"
            size="small"
            :loading="instanceBusy"
            @click="handleReloginInstance"
          >
            <Icon icon="ph:qr-code-bold" width="14" style="margin-right: 4px;" />
            {{ t('settings.ilinkRelogin') }}
          </el-button>

          <el-button
            v-if="instance.status === 'disabled' || !instance.enabled"
            size="small"
            type="primary"
            :loading="instanceBusy"
            @click="handleEnableInstance"
          >
            {{ t('settings.ilinkEnable') }}
          </el-button>
          <el-button
            v-else
            size="small"
            type="warning"
            plain
            :loading="instanceBusy"
            @click="handleDisableInstance"
          >
            {{ t('settings.ilinkDisable') }}
          </el-button>

          <el-popconfirm
            :title="t('settings.ilinkDeleteConfirm')"
            confirm-button-type="danger"
            @confirm="handleDeleteInstance"
          >
            <template #reference>
              <el-button size="small" type="danger" plain :loading="instanceBusy">
                {{ t('settings.ilinkDelete') }}
              </el-button>
            </template>
          </el-popconfirm>
        </div>
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
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { t } from '@/utils/locale'
import { imApi, renderQrContentToImage, type ImBindingItem } from '@/api/im'
import type { ImBotInstance, ImBotInstanceStatus } from '@/types'

const loading = ref(false)
const generating = ref(false)
const bindings = ref<ImBindingItem[]>([])
const bindToken = ref<string | null>(null)
const remainingSeconds = ref<number>(0)
const unbindingKey = ref<string | null>(null)
let timerId: ReturnType<typeof setInterval> | null = null

const instance = ref<ImBotInstance | null>(null)
const instanceBusy = ref(false)
const gatewayNotConfigured = ref(false)
const verifyCodeInput = ref('')
const qrDataUrl = ref('')
let pollTimer: ReturnType<typeof setInterval> | null = null
let lastQrSource = ''

const formattedRemainingTime = computed(() => {
  const m = Math.floor(remainingSeconds.value / 60)
  const s = remainingSeconds.value % 60
  return `${m}:${s < 10 ? '0' : ''}${s}`
})

function isLoginStage(status?: ImBotInstanceStatus): boolean {
  return (
    status === 'pending_login' ||
    status === 'qr_ready' ||
    status === 'scanned' ||
    status === 'need_verifycode'
  )
}

function statusLabel(status: ImBotInstanceStatus): string {
  switch (status) {
    case 'pending_login':
      return t('settings.ilinkStatusPending')
    case 'qr_ready':
      return t('settings.ilinkStatusQrReady')
    case 'scanned':
      return t('settings.ilinkStatusScanned')
    case 'need_verifycode':
      return t('settings.ilinkStatusNeedVerify')
    case 'active':
      return t('settings.ilinkStatusActive')
    case 'session_expired':
      return t('settings.ilinkStatusSessionExpired')
    case 'expired':
      return t('settings.ilinkStatusExpired')
    case 'error':
      return t('settings.ilinkStatusError')
    case 'disabled':
      return t('settings.ilinkStatusDisabled')
    default:
      return status
  }
}

function statusTagType(status: ImBotInstanceStatus): 'success' | 'warning' | 'danger' | 'info' {
  if (status === 'active') return 'success'
  if (status === 'error' || status === 'expired' || status === 'session_expired') return 'danger'
  if (status === 'disabled') return 'info'
  return 'warning'
}

const bookkeepingEnabled = computed(() => instance.value?.capability === 'query+confirm')

async function handleCapabilityChange(enabled: boolean) {
  if (!instance.value) return
  instanceBusy.value = true
  try {
    await imApi.setInstanceCapability(instance.value.id, enabled ? 'query+confirm' : 'readonly')
    ElMessage.success(t('settings.ilinkCapabilityUpdated'))
    await fetchInstances()
  } catch (err) {
    console.error('Failed to update iLink capability:', err)
    ElMessage.error(t('settings.ilinkCapabilityFailed'))
  } finally {
    instanceBusy.value = false
  }
}

function formatTime(val: string): string {
  if (!val) return '-'
  try {
    return new Date(val).toLocaleString()
  } catch {
    return val
  }
}

function stopInstancePolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

function syncInstancePolling() {
  if (instance.value && isLoginStage(instance.value.status)) {
    if (!pollTimer) {
      pollTimer = setInterval(() => {
        void pollCurrentInstance()
      }, 2000)
    }
  } else {
    stopInstancePolling()
  }
}

watch(
  () => instance.value?.qr_content,
  async (content) => {
    const val = content || ''
    if (!val) {
      lastQrSource = ''
      qrDataUrl.value = ''
      return
    }
    if (val === lastQrSource && qrDataUrl.value) return
    lastQrSource = val
    try {
      const dataUrl = await renderQrContentToImage(val)
      if (lastQrSource === val) {
        qrDataUrl.value = dataUrl
      }
    } catch (err) {
      console.error('Failed to render iLink QR code:', err)
      if (lastQrSource === val) {
        qrDataUrl.value = ''
      }
    }
  },
  { immediate: true }
)

async function pollCurrentInstance() {
  if (!instance.value) return
  try {
    const updated = await imApi.getInstance(instance.value.id)
    const prevStatus = instance.value.status
    instance.value = updated
    syncInstancePolling()
    if (prevStatus !== 'active' && updated.status === 'active') {
      await fetchBindings()
    }
  } catch {
    // Keep polling softly on transient errors
  }
}

async function fetchInstances() {
  try {
    const list = await imApi.listInstances()
    instance.value = Array.isArray(list) && list.length > 0 ? (list[0] ?? null) : null
    syncInstancePolling()
  } catch (err) {
    console.error('Failed to load iLink instances:', err)
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

async function refreshAll() {
  await Promise.all([fetchInstances(), fetchBindings()])
}

async function handleCreateInstance() {
  instanceBusy.value = true
  gatewayNotConfigured.value = false
  try {
    const created = await imApi.createInstance({ platform: 'wechat', adapter: 'ilink' })
    instance.value = created
    syncInstancePolling()
  } catch (err: any) {
    const msg = err?.response?.data?.message || ''
    if (String(msg).includes('not configured') || String(msg).includes('未配置')) {
      gatewayNotConfigured.value = true
    }
  } finally {
    instanceBusy.value = false
  }
}

async function handleReloginInstance() {
  if (!instance.value) return
  instanceBusy.value = true
  try {
    await imApi.reloginInstance(instance.value.id)
    verifyCodeInput.value = ''
    await fetchInstances()
  } finally {
    instanceBusy.value = false
  }
}

async function handleSubmitVerifyCode() {
  if (!instance.value) return
  const code = verifyCodeInput.value.trim()
  if (!/^\d{4,8}$/.test(code)) {
    ElMessage.warning(t('settings.ilinkVerifyInvalid'))
    return
  }
  instanceBusy.value = true
  try {
    await imApi.verifyInstanceCode(instance.value.id, code)
    verifyCodeInput.value = ''
    ElMessage.success(t('settings.ilinkVerifySubmitted'))
    await pollCurrentInstance()
  } finally {
    instanceBusy.value = false
  }
}

async function handleEnableInstance() {
  if (!instance.value) return
  instanceBusy.value = true
  try {
    await imApi.enableInstance(instance.value.id)
    await fetchInstances()
  } finally {
    instanceBusy.value = false
  }
}

async function handleDisableInstance() {
  if (!instance.value) return
  instanceBusy.value = true
  try {
    await imApi.disableInstance(instance.value.id)
    await fetchInstances()
  } finally {
    instanceBusy.value = false
  }
}

async function handleDeleteInstance() {
  if (!instance.value) return
  instanceBusy.value = true
  try {
    await imApi.deleteInstance(instance.value.id)
    instance.value = null
    qrDataUrl.value = ''
    lastQrSource = ''
    stopInstancePolling()
    ElMessage.info(t('settings.ilinkGatewayOfflineDeleteHint'))
    await fetchBindings()
  } finally {
    instanceBusy.value = false
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
        clearInterval(timerId!)
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
  void refreshAll()
})

onUnmounted(() => {
  if (timerId) clearInterval(timerId)
  stopInstancePolling()
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

.ilink-section {
  background: var(--mf-card-bg, #ffffff);
  border: 1px solid var(--mf-border, #e5e7eb);
  border-radius: 12px;
  padding: 18px 20px;
  margin-bottom: 24px;
}

.ilink-header {
  margin-bottom: 14px;
}

.ilink-title-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}

.ilink-title-row h4 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.ilink-wechat-icon {
  color: #07c160;
}

.ilink-empty {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}

.ilink-notice {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--mf-text-muted);
}

.ilink-login-box {
  display: flex;
  align-items: center;
  gap: 20px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}

.ilink-qr-wrap {
  padding: 8px;
  background: #ffffff;
  border: 1px solid var(--mf-border, #e5e7eb);
  border-radius: 10px;
}

.ilink-qr-img {
  width: 200px;
  height: 200px;
  display: block;
}

.ilink-qr-placeholder {
  width: 200px;
  height: 200px;
  border: 1px dashed var(--mf-border, #d1d5db);
  border-radius: 10px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 12px;
  text-align: center;
  font-size: 12px;
  color: var(--mf-text-muted);
}

.ilink-login-meta {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.ilink-stage-text {
  margin: 0;
  font-size: 14px;
  font-weight: 500;
  color: var(--mf-text-main);
}

.ilink-verify-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.ilink-meta-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 16px 28px;
  margin-bottom: 14px;
  font-size: 13px;
}

.ilink-meta-item {
  display: flex;
  align-items: center;
  gap: 8px;
}

.meta-label {
  color: var(--mf-text-muted);
}

.ilink-capability-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
  padding: 10px 12px;
  margin-bottom: 14px;
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
}

.ilink-capability-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 13px;
  min-width: 0;
}

.ilink-capability-value {
  font-weight: 600;
}

.ilink-capability-hint {
  color: var(--mf-text-muted);
  font-size: 12px;
  line-height: 1.5;
  max-width: 520px;
}

.ilink-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
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
