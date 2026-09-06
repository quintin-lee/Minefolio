<template>
  <div class="panel-container">
    <div class="panel-header" style="display: flex; justify-content: space-between; align-items: center;">
      <div style="display: flex; align-items: center; gap: 10px;">
        <h3>{{ t('settings.twoFactorTitle') }}</h3>
        <el-tag :type="twoFactorEnabled ? 'success' : 'info'" size="small" effect="dark">
          {{ twoFactorEnabled ? t('settings.twoFactorStatusOn') : t('settings.twoFactorStatusOff') }}
        </el-tag>
      </div>
      <div class="header-actions">
        <el-button
          v-if="!twoFactorEnabled"
          type="primary"
          size="small"
          class="action-btn"
          @click="openTwoFactorSetup"
          :loading="twoFactorLoading"
        >
          {{ t('settings.enableTwoFactor') }}
        </el-button>
        <el-button
          v-else
          type="danger"
          plain
          size="small"
          @click="handleDisableTwoFactor"
          :loading="twoFactorLoading"
        >
          {{ t('settings.disableTwoFactor') }}
        </el-button>
      </div>
    </div>
    <p class="export-hint">
      {{ t('settings.twoFactorDesc') }}
    </p>

    <el-dialog
      v-model="twoFactorSetupVisible"
      :title="t('settings.setupTwoFactorTitle')"
      width="460px"
      append-to-body
      :close-on-click-modal="false"
    >
      <div class="two-factor-setup-content">
        <div class="setup-step">
          <span class="step-num">1</span>
          <span class="step-desc">{{ t('settings.stepScanHint') }}</span>
        </div>

        <div class="qr-canvas-wrapper">
          <canvas ref="qrCanvasRef" class="qr-canvas"></canvas>
        </div>

        <div class="setup-step">
          <span class="step-num">2</span>
          <span class="step-desc">{{ t('settings.stepManualHint') }}</span>
        </div>

        <div class="secret-copy-box">
          <span class="secret-text">{{ twoFactorSecret }}</span>
          <el-button link type="primary" size="small" @click="copySecret">{{ t('settings.copy') }}</el-button>
        </div>

        <div class="setup-step" style="margin-top: 16px;">
          <span class="step-num">3</span>
          <span class="step-desc">{{ t('settings.stepVerifyHint') }}</span>
        </div>

        <el-input
          v-model="twoFactorVerifyCode"
          :placeholder="t('settings.twoFactorCodePlaceholder')"
          maxlength="6"
          size="large"
          style="margin-top: 10px; font-family: monospace; font-size: 16px; text-align: center;"
          @keyup.enter="confirmEnableTwoFactor"
        />
      </div>

      <template #footer>
        <div class="dialog-footer">
          <el-button @click="twoFactorSetupVisible = false">{{ t('common.cancel') }}</el-button>
          <el-button type="primary" :loading="twoFactorLoading" @click="confirmEnableTwoFactor">
            {{ t('settings.confirmActivate') }}
          </el-button>
        </div>
      </template>
    </el-dialog>

    <el-dialog
      v-model="backupCodesVisible"
      :title="t('settings.backupCodesTitle')"
      width="480px"
      append-to-body
      :close-on-click-modal="false"
    >
      <div class="backup-codes-content">
        <el-alert
          :title="t('settings.backupCodesAlertTitle')"
          type="warning"
          :closable="false"
          :description="t('settings.backupCodesAlertDesc')"
          show-icon
          style="margin-bottom: 16px;"
        />

        <div class="backup-codes-grid">
          <div v-for="(code, idx) in backupCodes" :key="idx" class="backup-code-item">
            <span class="code-idx">{{ idx + 1 }}.</span>
            <span class="code-val">{{ code }}</span>
          </div>
        </div>
      </div>

      <template #footer>
        <div class="dialog-footer" style="display: flex; justify-content: space-between;">
          <el-button type="info" plain @click="downloadBackupCodes">{{ t('settings.downloadBackupCodes') }}</el-button>
          <div style="display: flex; gap: 8px;">
            <el-button type="primary" plain @click="copyBackupCodes">{{ t('settings.copyAll') }}</el-button>
            <el-button type="primary" @click="backupCodesVisible = false">{{ t('settings.backupCodesSaved') }}</el-button>
          </div>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { authApi } from '@/api/auth'
import { useAuthStore } from '@/stores/auth'
import { t } from '@/utils/locale'

const auth = useAuthStore()
const twoFactorEnabled = ref(false)
const twoFactorLoading = ref(false)
const twoFactorSetupVisible = ref(false)
const twoFactorSecret = ref('')
const twoFactorOtpauthUrl = ref('')
const twoFactorVerifyCode = ref('')
const qrCanvasRef = ref<HTMLCanvasElement | null>(null)
const backupCodesVisible = ref(false)
const backupCodes = ref<string[]>([])

onMounted(() => {
  loadTwoFactorStatus()
})

async function loadTwoFactorStatus() {
  try {
    const raw = await authApi.get2FaStatus()
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { enabled: boolean } }).data : raw) as { enabled: boolean }
    twoFactorEnabled.value = !!res?.enabled
  } catch {
    // ignore
  }
}

async function openTwoFactorSetup() {
  twoFactorLoading.value = true
  try {
    const raw = await authApi.setup2Fa()
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { secret: string; otpauth_url: string } }).data : raw) as { secret: string; otpauth_url: string }
    twoFactorSecret.value = res.secret
    twoFactorOtpauthUrl.value = res.otpauth_url
    twoFactorVerifyCode.value = ''
    twoFactorSetupVisible.value = true
    setTimeout(async () => {
      if (qrCanvasRef.value) {
        const QRCode = await import('qrcode')
        QRCode.toCanvas(qrCanvasRef.value, res.otpauth_url, {
          width: 180,
          margin: 2,
          color: {
            dark: '#000000',
            light: '#ffffff'
          }
        })
      }
    }, 100)
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || t('settings.secretFetchFailed'))
  } finally {
    twoFactorLoading.value = false
  }
}

async function copySecret() {
  if (!twoFactorSecret.value) return
  await navigator.clipboard.writeText(twoFactorSecret.value)
  ElMessage.success(t('settings.secretCopied'))
}

async function confirmEnableTwoFactor() {
  if (!twoFactorVerifyCode.value.trim() || twoFactorVerifyCode.value.trim().length !== 6) {
    ElMessage.warning(t('settings.twoFactorCodePlaceholder'))
    return
  }
  twoFactorLoading.value = true
  try {
    const raw = await authApi.enable2Fa(twoFactorVerifyCode.value.trim())
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { backup_codes: string[] } }).data : raw) as { backup_codes: string[] }
    twoFactorEnabled.value = true
    twoFactorSetupVisible.value = false
    backupCodes.value = res.backup_codes || []
    backupCodesVisible.value = true
    ElMessage.success(t('settings.twoFactorEnabledMsg'))
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || t('settings.activationFailed'))
  } finally {
    twoFactorLoading.value = false
  }
}

async function handleDisableTwoFactor() {
  try {
    await ElMessageBox.confirm(t('settings.disableConfirmMsg'), t('settings.disableConfirmTitle'), {
      confirmButtonText: t('settings.confirmDisable'),
      cancelButtonText: t('common.cancel'),
      type: 'warning'
    })
    twoFactorLoading.value = true
    await authApi.disable2Fa()
    twoFactorEnabled.value = false
    ElMessage.success(t('settings.twoFactorDisabled'))
  } catch (e: any) {
    if (e !== 'cancel') {
      ElMessage.error(e?.response?.data?.message || t('settings.disableFailed'))
    }
  } finally {
    twoFactorLoading.value = false
  }
}

async function copyBackupCodes() {
  if (!backupCodes.value.length) return
  const text = `${t('settings.backupFileHeader')}\n\n` + backupCodes.value.map((c, i) => `${i + 1}. ${c}`).join('\n')
  await navigator.clipboard.writeText(text)
  ElMessage.success(t('settings.backupCodesCopied'))
}

function downloadBackupCodes() {
  if (!backupCodes.value.length) return
  const text = `${t('settings.backupFileHeader')}\n\n` + backupCodes.value.map((c, i) => `${i + 1}. ${c}`).join('\n')
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `minefolio-2fa-backup-codes-${auth.user?.username || 'user'}.txt`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
.setup-step {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  width: 100%;
  margin-bottom: 8px;
}

.step-num {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: var(--mf-primary);
  color: #fff;
  font-size: 12px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.step-desc {
  font-size: 13px;
  color: var(--mf-text-main);
  line-height: 1.5;
}

.qr-canvas-wrapper {
  background: #ffffff;
  padding: 12px;
  border-radius: 8px;
  margin: 8px 0 16px;
  box-shadow: var(--mf-shadow-sm);
}

.qr-canvas {
  display: block;
}

.secret-copy-box {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}

.secret-text {
  font-family: 'JetBrains Mono', ui-monospace, monospace;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-primary);
  letter-spacing: 1px;
}

.backup-codes-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  padding: 16px;
  border-radius: 8px;
}

.backup-code-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: 'JetBrains Mono', ui-monospace, monospace;
  font-size: 14px;
}

.code-idx {
  color: var(--mf-text-muted);
  width: 20px;
}

.code-val {
  font-weight: 600;
  color: var(--mf-text-main);
  background: rgba(0, 212, 255, 0.08);
  padding: 2px 6px;
  border-radius: 4px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
