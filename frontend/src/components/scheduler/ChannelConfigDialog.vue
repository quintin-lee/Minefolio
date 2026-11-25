<template>
  <el-dialog
    v-model="visible"
    title="个人微信推送通道配置"
    :width="dialogWidth"
    destroy-on-close
    :close-on-click-modal="false"
    @close="handleDialogClose"
  >
    <div v-loading="loading" class="channel-dialog-content">
      <el-alert
        type="info"
        :closable="false"
        show-icon
        class="guide-alert"
      >
        <template #title>
          <span>绑定个人微信，定时任务产出的投研研报与波动警报将直接推送到您的手机微信。</span>
        </template>
      </el-alert>

      <el-form
        ref="formRef"
        :model="form"
        :rules="rules"
        label-position="top"
        class="channel-form"
      >
        <el-form-item label="推送服务类型" prop="channel_type">
          <el-radio-group v-model="form.channel_type" class="channel-type-group">
            <el-radio-button value="serverchan">
              <span class="radio-label">
                <Icon icon="ph:paper-plane-tilt-bold" class="radio-icon" />
                Server酱 (Turbo)
              </span>
            </el-radio-button>
            <el-radio-button value="pushdeer">
              <span class="radio-label">
                <Icon icon="ph:bell-simple-ringing-bold" class="radio-icon" />
                PushDeer
              </span>
            </el-radio-button>
            <el-radio-button value="wechat_work">
              <span class="radio-label">
                <Icon icon="ph:chats-circle-bold" class="radio-icon" />
                企微机器人
              </span>
            </el-radio-button>
            <el-radio-button value="webhook">
              <span class="radio-label">
                <Icon icon="ph:plugs-connected-bold" class="radio-icon" />
                通用 Webhook
              </span>
            </el-radio-button>
            <el-radio-button value="wechat_ilink">
              <span class="radio-label">
                <Icon icon="ph:wechat-logo-bold" class="radio-icon" />
                微信 iLink
              </span>
            </el-radio-button>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="通道显示备注" prop="channel_name">
          <el-input
            v-model="form.channel_name"
            placeholder="例如：我的常用微信"
            maxlength="50"
            clearable
          />
        </el-form-item>

        <!-- ServerChan Key -->
        <template v-if="form.channel_type === 'serverchan'">
          <el-form-item label="Server酱 SendKey" prop="secret_key">
            <div v-if="existingKeyMasked" class="existing-key-hint">
              <Icon icon="ph:shield-check-bold" class="key-icon" />
              当前已绑定密钥: <code>{{ existingKeyMasked }}</code> (如保持不变请留空)
            </div>
            <el-input
              v-model="form.secret_key"
              type="password"
              show-password
              :placeholder="existingKeyMasked ? '留空表示使用当前已配置密钥，输入新Key覆盖' : '请输入以 SCT 开头的 SendKey'"
            />
            <div class="field-tip">
              前往 <a href="https://sct.ftqq.com" target="_blank" rel="noopener noreferrer">Server酱·Turbo版官网</a> 登录微信扫码即可获取专属 SendKey。
            </div>
          </el-form-item>
        </template>

        <!-- PushDeer Key -->
        <template v-if="form.channel_type === 'pushdeer'">
          <el-form-item label="PushDeer PushKey" prop="secret_key">
            <div v-if="existingKeyMasked" class="existing-key-hint">
              <Icon icon="ph:shield-check-bold" class="key-icon" />
              当前已绑定密钥: <code>{{ existingKeyMasked }}</code> (如保持不变请留空)
            </div>
            <el-input
              v-model="form.secret_key"
              type="password"
              show-password
              :placeholder="existingKeyMasked ? '留空表示使用当前已配置密钥，输入新Key覆盖' : '请输入 PushDeer 生成的 PushKey'"
            />
            <div class="field-tip">
              PushDeer 支持苹果/安卓客户端及快应用推送，请至 <a href="https://www.pushdeer.com" target="_blank" rel="noopener noreferrer">PushDeer 官网</a> 或小程序扫码获取 Key。
            </div>
          </el-form-item>
        </template>

        <!-- WeChat Work / Webhook URL -->
        <template v-if="form.channel_type === 'wechat_work' || form.channel_type === 'webhook'">
          <el-form-item
            :label="form.channel_type === 'wechat_work' ? '企业微信群机器人 Webhook 地址' : '通用 Webhook 地址 (POST)'"
            prop="webhook_url"
          >
            <el-input
              v-model="form.webhook_url"
              placeholder="https://..."
              clearable
            />
            <div class="field-tip">
              {{ form.channel_type === 'wechat_work'
                ? '在企微群中添加“群机器人”，复制其提供的 Webhook 完整链接。'
                : '系统将通过 HTTP POST 请求向此地址推送包含 title, content, markdown 结构的 JSON 载荷。' }}
            </div>
          </el-form-item>
        </template>

        <!-- WeChat iLink Readiness -->
        <template v-if="form.channel_type === 'wechat_ilink'">
          <el-alert
            :type="ilinkReadiness.level === 'success' ? 'success' : ilinkReadiness.level === 'danger' ? 'error' : 'warning'"
            :closable="false"
            show-icon
          >
            <template #title>
              <span v-if="ilinkReadiness.reason === 'ready'">
                微信 iLink 实例已就绪（<template v-if="ilinkStatus?.ilink_bot_id">机器人 {{ ilinkStatus.ilink_bot_id }} · </template>最近会话 {{ formatIlinkInboundTime(ilinkStatus?.last_inbound_at) }}），无需密钥或 Webhook URL。
              </span>
              <span v-else-if="ilinkReadiness.reason === 'not_bound'">
                尚未绑定微信 iLink 机器人实例，请先前往「系统设置 → IM 机器人」扫码绑定。
              </span>
              <span v-else-if="ilinkReadiness.reason === 'needs_inbound'">
                实例已激活，但需先在微信向该机器人发送任意一条消息以激活推送上下文（过期后将自动等待最多 30 分钟）。
              </span>
              <span v-else>
                当前 iLink 实例状态为「{{ getIlinkStatusLabel(ilinkReadiness.reason) }}」，请前往「系统设置 → IM 机器人」启用或重新扫码。
              </span>
            </template>
          </el-alert>
          <div
            v-if="ilinkReadiness.level !== 'success' && ilinkReadiness.reason !== 'needs_inbound'"
            class="ilink-action-row"
          >
            <el-button size="small" type="primary" plain @click="goBindIlink">
              前往设置绑定微信机器人
            </el-button>
          </div>
        </template>
      </el-form>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button
          plain
          :loading="testing"
          :disabled="loading || saving || form.channel_type === 'none'"
          @click="handleTest"
        >
          <Icon icon="ph:paper-plane-right" class="btn-icon" />
          发送测试消息
        </el-button>
        <div class="footer-right">
          <el-button @click="visible = false">取消</el-button>
          <el-button
            type="primary"
            :loading="saving"
            :disabled="loading || testing"
            @click="handleSave"
          >
            保存配置
          </el-button>
        </div>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { schedulerApi } from '@/api/scheduler'
import {
  classifyIlinkChannelStatus,
  formatIlinkInboundTime,
  getIlinkStatusLabel
} from '@/utils/ilink-channel'
import type { ChannelType, IlinkChannelStatus } from '@/types'

const emit = defineEmits<{
  (e: 'saved'): void
}>()

const router = useRouter()
const visible = ref(false)
const loading = ref(false)
const saving = ref(false)
const testing = ref(false)
const existingKeyMasked = ref('')
const ilinkStatus = ref<IlinkChannelStatus | null>(null)
const ilinkReadiness = computed(() => classifyIlinkChannelStatus(ilinkStatus.value))
let openSeq = 0
let testAbortController: AbortController | null = null

const abortTestRequest = () => {
  if (testAbortController) {
    testAbortController.abort()
    testAbortController = null
  }
  testing.value = false
}

const handleDialogClose = () => {
  openSeq++
  abortTestRequest()
}

watch(visible, (val) => {
  if (!val) {
    handleDialogClose()
  }
})

const goBindIlink = () => {
  visible.value = false
  router.push({ path: '/settings', query: { tab: 'im-bot' } })
}
const windowWidth = ref(typeof window !== 'undefined' ? window.innerWidth : 1024)

const updateWindowWidth = () => {
  windowWidth.value = window.innerWidth
}

onMounted(() => {
  window.addEventListener('resize', updateWindowWidth)
})

onUnmounted(() => {
  window.removeEventListener('resize', updateWindowWidth)
  openSeq++
  abortTestRequest()
})

const dialogWidth = computed(() => (windowWidth.value < 640 ? '94vw' : '560px'))

const formRef = ref<FormInstance>()

const form = reactive<{
  channel_type: ChannelType
  channel_name: string
  webhook_url: string
  secret_key: string
}>({
  channel_type: 'serverchan',
  channel_name: '我的微信提醒',
  webhook_url: '',
  secret_key: ''
})

const rules: FormRules = {
  channel_name: [
    { required: true, message: '请输入通道备注名称', trigger: 'blur' }
  ],
  webhook_url: [
    {
      validator: (_rule, value, callback) => {
        if (form.channel_type === 'wechat_work' || form.channel_type === 'webhook') {
          if (!value || !value.trim()) {
            return callback(new Error('请输入 Webhook 完整的 URL 地址'))
          }
          if (!value.startsWith('http://') && !value.startsWith('https://')) {
            return callback(new Error('Webhook 地址必须以 http:// 或 https:// 开头'))
          }
        }
        callback()
      },
      trigger: 'blur'
    }
  ]
}

const isRequestAborted = (err: unknown): boolean => {
  if (!err || typeof err !== 'object') return false
  const e = err as { code?: string; name?: string }
  return e.code === 'ERR_CANCELED' || e.name === 'CanceledError' || e.name === 'AbortError'
}

const isHttpHandledError = (err: unknown): boolean => {
  if (!err || typeof err !== 'object') return false
  const e = err as { isAxiosError?: boolean; response?: { data?: { code?: unknown } } }
  return Boolean(e.isAxiosError || e.response?.data?.code !== undefined)
}

const notifyFallbackError = (err: unknown, fallback: string) => {
  if (isRequestAborted(err) || isHttpHandledError(err)) {
    return
  }
  const msg = (err as { message?: string } | null | undefined)?.message
  ElMessage.error(msg || fallback)
}

const open = async () => {
  const seq = ++openSeq
  visible.value = true
  loading.value = true
  existingKeyMasked.value = ''
  ilinkStatus.value = null
  form.channel_type = 'serverchan'
  form.channel_name = '我的微信提醒'
  form.webhook_url = ''
  form.secret_key = ''
  formRef.value?.clearValidate()

  try {
    const res = await schedulerApi.getChannel()
    if (seq !== openSeq || !visible.value) return
    ilinkStatus.value = res?.ilink_status || null
    if (res && res.channel_type && res.channel_type !== 'none') {
      form.channel_type = res.channel_type
      form.channel_name = res.channel_name || '我的微信提醒'
      form.webhook_url = res.webhook_url || ''
      existingKeyMasked.value = res.secret_key_masked || ''
    }
  } catch (err: unknown) {
    if (seq !== openSeq || !visible.value) return
    notifyFallbackError(err, '获取通道配置失败')
  } finally {
    if (seq === openSeq) {
      loading.value = false
    }
  }
}

const buildSavePayload = () => ({
  channel_type: form.channel_type,
  channel_name: form.channel_name,
  webhook_url: form.channel_type === 'wechat_ilink' ? '' : form.webhook_url,
  secret_key:
    form.channel_type === 'wechat_ilink'
      ? undefined
      : form.secret_key.trim()
        ? form.secret_key.trim()
        : undefined
})

const validateFormAndKey = async (): Promise<boolean> => {
  if (!formRef.value) return false
  const valid = await formRef.value.validate().then(
    () => true,
    () => false
  )
  if (!valid) return false

  if (
    (form.channel_type === 'serverchan' || form.channel_type === 'pushdeer') &&
    !form.secret_key.trim() &&
    !existingKeyMasked.value
  ) {
    ElMessage.warning('请填写推送秘钥 Key')
    return false
  }
  return true
}

const handleSave = async () => {
  if (!(await validateFormAndKey())) return

  saving.value = true
  try {
    await schedulerApi.saveChannel(buildSavePayload())
    ElMessage.success('微信推送通道配置已保存')
    visible.value = false
    emit('saved')
  } catch (err: unknown) {
    notifyFallbackError(err, '保存通道配置失败')
  } finally {
    saving.value = false
  }
}

const handleTest = async () => {
  if (!(await validateFormAndKey())) return

  abortTestRequest()
  const controller = new AbortController()
  testAbortController = controller
  testing.value = true
  try {
    // If user edited key/url or switched to wechat_ilink, persist first so test utilizes newest value
    if (form.channel_type === 'wechat_ilink' || form.secret_key.trim() || form.webhook_url) {
      await schedulerApi.saveChannel(buildSavePayload())
      if (form.channel_type === 'wechat_ilink') {
        existingKeyMasked.value = ''
        form.webhook_url = ''
      }
      emit('saved')
    }
    if (controller.signal.aborted) return
    const res = await schedulerApi.testChannel({ signal: controller.signal })
    if (controller.signal.aborted) return
    ElMessage.success(res?.message || '测试消息发送成功，请在微信中查收！')
  } catch (err: unknown) {
    notifyFallbackError(err, '测试发送失败，请检查配置是否正确')
  } finally {
    if (testAbortController === controller) {
      testAbortController = null
      testing.value = false
    }
  }
}

defineExpose({
  open
})
</script>

<style scoped>
.channel-dialog-content {
  min-height: 220px;
}

.guide-alert {
  margin-bottom: 20px;
}

.channel-type-group {
  display: flex;
  width: 100%;
  flex-wrap: wrap;
  gap: 6px;
}

.channel-type-group :deep(.el-radio-button) {
  flex: 1 1 140px;
}

.channel-type-group :deep(.el-radio-button .el-radio-button__inner) {
  width: 100%;
  padding: 10px 8px;
  border: 1px solid var(--mf-border, var(--el-border-color));
  border-radius: var(--el-border-radius-base, 6px);
}

.radio-label {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  font-size: 13px;
}

.radio-icon {
  font-size: 16px;
}

.existing-key-hint {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--el-color-success);
  margin-bottom: 6px;
  background: var(--mf-success-light, rgba(16, 185, 129, 0.1));
  padding: 4px 8px;
  border-radius: 4px;
}

.key-icon {
  font-size: 14px;
}

.existing-key-hint code {
  font-family: var(--font-mono, monospace);
  font-weight: 600;
}

.field-tip {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: 6px;
  line-height: 1.5;
}

.field-tip a {
  color: var(--el-color-primary);
  text-decoration: none;
}

.field-tip a:hover {
  text-decoration: underline;
}

.ilink-action-row {
  margin-top: 10px;
}

.dialog-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.footer-right {
  display: flex;
  gap: 12px;
}

.btn-icon {
  margin-right: 4px;
}
</style>
