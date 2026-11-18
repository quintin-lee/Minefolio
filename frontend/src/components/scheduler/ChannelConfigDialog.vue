<template>
  <el-dialog
    v-model="visible"
    title="个人微信推送通道配置"
    width="560px"
    destroy-on-close
    :close-on-click-modal="false"
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
                企业微信机器人
              </span>
            </el-radio-button>
            <el-radio-button value="webhook">
              <span class="radio-label">
                <Icon icon="ph:plugs-connected-bold" class="radio-icon" />
                通用 Webhook
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
            <el-input
              v-model="form.secret_key"
              type="password"
              show-password
              :placeholder="existingKeyMasked ? `已配置: ${existingKeyMasked} (如需更新请输入新Key)` : '请输入以 SCT 开头的 SendKey'"
            />
            <div class="field-tip">
              前往 <a href="https://sct.ftqq.com" target="_blank" rel="noopener noreferrer">Server酱·Turbo版官网</a> 登录微信扫码即可获取专属 SendKey。
            </div>
          </el-form-item>
        </template>

        <!-- PushDeer Key -->
        <template v-if="form.channel_type === 'pushdeer'">
          <el-form-item label="PushDeer PushKey" prop="secret_key">
            <el-input
              v-model="form.secret_key"
              type="password"
              show-password
              :placeholder="existingKeyMasked ? `已配置: ${existingKeyMasked} (如需更新请输入新Key)` : '请输入 PushDeer 生成的 PushKey'"
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
      </el-form>
    </div>

    <template #footer>
      <div class="dialog-footer">
        <el-button
          plain
          :loading="testing"
          :disabled="loading || form.channel_type === 'none'"
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
            :disabled="loading"
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
import { ref, reactive } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { schedulerApi } from '@/api/scheduler'
import type { ChannelType } from '@/types'

const emit = defineEmits<{
  (e: 'saved'): void
}>()

const visible = ref(false)
const loading = ref(false)
const saving = ref(false)
const testing = ref(false)
const existingKeyMasked = ref('')

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

const open = async () => {
  visible.value = true
  loading.value = true
  existingKeyMasked.value = ''
  form.secret_key = ''

  try {
    const res = await schedulerApi.getChannel()
    if (res && res.channel_type && res.channel_type !== 'none') {
      form.channel_type = res.channel_type
      form.channel_name = res.channel_name || '我的微信提醒'
      form.webhook_url = res.webhook_url || ''
      existingKeyMasked.value = res.secret_key_masked || ''
    } else {
      form.channel_type = 'serverchan'
      form.channel_name = '我的微信提醒'
      form.webhook_url = ''
    }
  } catch (err: any) {
    ElMessage.error(err.message || '获取通道配置失败')
  } finally {
    loading.value = false
  }
}

const handleSave = async () => {
  if (!formRef.value) return
  await formRef.value.validate()

  if (
    (form.channel_type === 'serverchan' || form.channel_type === 'pushdeer') &&
    !form.secret_key.trim() &&
    !existingKeyMasked.value
  ) {
    ElMessage.warning('请填写推送秘钥 Key')
    return
  }

  saving.value = true
  try {
    await schedulerApi.saveChannel({
      channel_type: form.channel_type,
      channel_name: form.channel_name,
      webhook_url: form.webhook_url,
      secret_key: form.secret_key.trim() ? form.secret_key.trim() : undefined
    })
    ElMessage.success('微信推送通道配置已保存')
    visible.value = false
    emit('saved')
  } catch (err: any) {
    ElMessage.error(err.message || '保存通道配置失败')
  } finally {
    saving.value = false
  }
}

const handleTest = async () => {
  testing.value = true
  try {
    // If user edited key/url, prompt or persist first
    if (form.secret_key.trim() || form.webhook_url) {
      await schedulerApi.saveChannel({
        channel_type: form.channel_type,
        channel_name: form.channel_name,
        webhook_url: form.webhook_url,
        secret_key: form.secret_key.trim() ? form.secret_key.trim() : undefined
      })
    }
    const res = await schedulerApi.testChannel()
    ElMessage.success(res?.message || '测试消息发送成功，请在微信中查收！')
  } catch (err: any) {
    ElMessage.error(err.message || '测试发送失败，请检查配置是否正确')
  } finally {
    testing.value = false
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
}

.channel-type-group :deep(.el-radio-button) {
  flex: 1;
}

.channel-type-group :deep(.el-radio-button__inner) {
  width: 100%;
  padding: 10px 8px;
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

.dialog-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.footer-right {
  display: flex;
  gap: 12px;
}

.btn-icon {
  margin-right: 4px;
}
</style>
