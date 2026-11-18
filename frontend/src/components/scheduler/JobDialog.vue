<template>
  <el-dialog
    v-model="visible"
    :title="isEdit ? '编辑定时任务' : '新建定时任务'"
    width="680px"
    destroy-on-close
    :close-on-click-modal="false"
  >
    <el-tabs v-model="activeTab" class="job-mode-tabs" :before-leave="handleTabChange">
      <el-tab-pane name="ai_prompt">
        <template #label>
          <span class="tab-label">
            <Icon icon="ph:sparkle-bold" class="tab-icon ai-icon" />
            AI 智能任务 (Prompt 驱动)
          </span>
        </template>
      </el-tab-pane>
      <el-tab-pane name="structured_report">
        <template #label>
          <span class="tab-label">
            <Icon icon="ph:chart-line-up-bold" class="tab-icon report-icon" />
            结构化报表任务 (表单驱动)
          </span>
        </template>
      </el-tab-pane>
    </el-tabs>

    <el-form
      ref="formRef"
      :model="form"
      :rules="rules"
      label-position="top"
      class="job-form"
    >
      <!-- Base Info -->
      <div class="form-section-title">基本信息</div>
      <el-row :gutter="16">
        <el-col :span="16">
          <el-form-item label="任务名称" prop="name">
            <el-input
              v-model="form.name"
              placeholder="例如：每日早晨持仓早报"
              maxlength="50"
              clearable
            />
          </el-form-item>
        </el-col>
        <el-col :span="8">
          <el-form-item label="触发机制" prop="trigger_type">
            <el-select v-model="form.trigger_type" style="width: 100%">
              <el-option label="Cron 定时触发" value="cron" />
              <el-option label="市场事件驱动" value="market_event" />
            </el-select>
          </el-form-item>
        </el-col>
      </el-row>

      <el-form-item label="任务描述" prop="description">
        <el-input
          v-model="form.description"
          placeholder="简要描述任务目标与运行周期（选填）"
          maxlength="120"
          clearable
        />
      </el-form-item>

      <!-- Trigger Config -->
      <div class="form-section-title">触发与调度配置</div>
      <template v-if="form.trigger_type === 'cron'">
        <el-form-item label="常用周期快捷选择">
          <el-radio-group v-model="cronPreset" size="small" @change="applyCronPreset">
            <el-radio-button value="0 8 * * *">每天早晨 08:00</el-radio-button>
            <el-radio-button value="0 18 * * 1-5">工作日收盘 18:00</el-radio-button>
            <el-radio-button value="0 21 * * *">每天晚间 21:00</el-radio-button>
            <el-radio-button value="0 9 * * 1">每周一 09:00</el-radio-button>
            <el-radio-button value="custom">自定义 Cron</el-radio-button>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="Cron 表达式 (5位: 分 时 日 月 周)" prop="cron_expr">
          <el-input
            v-model="form.cron_expr"
            placeholder="例如: 0 8 * * *"
            @input="onCronInput"
          >
            <template #prepend>
              <Icon icon="ph:clock-bold" />
            </template>
          </el-input>
          <div class="field-tip">
            支持标准五段式 Cron。常用格式：<code>0 8 * * *</code> 每天 08:00；<code>0 18 * * 1-5</code> 周一至周五 18:00。
          </div>
        </el-form-item>
      </template>

      <template v-else>
        <el-form-item label="关联市场事件类型" prop="event_name">
          <el-select v-model="form.event_name" style="width: 100%">
            <el-option label="每日收盘汇总事件 (daily_summary_event)" value="daily_summary_event" />
            <el-option label="持仓标的异动预警 (price_spike_alert)" value="price_spike_alert" />
            <el-option label="定投划转执行事件 (dca_execution_event)" value="dca_execution_event" />
          </el-select>
          <div class="field-tip">
            当系统行情引擎检测到该市场事件时立即触发，同事件内置 4 小时防刷频冷却机制。
          </div>
        </el-form-item>
      </template>

      <!-- Task Payload Config -->
      <div class="form-section-title">
        {{ activeTab === 'ai_prompt' ? 'AI 智能任务指令配置' : '结构化报表生成选项' }}
      </div>

      <template v-if="activeTab === 'ai_prompt'">
        <el-form-item label="AI 提示词 (Prompt)" prop="ai_prompt">
          <el-input
            v-model="form.ai_prompt"
            type="textarea"
            :rows="4"
            placeholder="下达给 AI Agent 的分析指令，例如：分析当前投资组合的集中度与风险暴露，结合近期市场大盘走势，生成一份精炼的投资早报。"
          />
        </el-form-item>

        <el-row :gutter="16">
          <el-col :span="12">
            <div class="switch-card">
              <div class="switch-info">
                <span class="switch-title">注入持仓上下文</span>
                <span class="switch-desc">自动将个人当前持仓资产、成本与市值注入模型</span>
              </div>
              <el-switch v-model="form.with_holdings" />
            </div>
          </el-col>
          <el-col :span="12">
            <div class="switch-card">
              <div class="switch-info">
                <span class="switch-title">全网联网搜索</span>
                <span class="switch-desc">允许 AI Agent 联网检索实时财经资讯与研报</span>
              </div>
              <el-switch v-model="form.with_web_search" />
            </div>
          </el-col>
        </el-row>
      </template>

      <template v-else>
        <el-form-item label="报表模板类型" prop="report_type">
          <el-select v-model="form.report_type" style="width: 100%">
            <el-option label="每日资产盘后速报 (Daily Closing Report)" value="daily_closing_report" />
            <el-option label="周度收益与归因分析 (Weekly PnL Summary)" value="weekly_pnl_summary" />
            <el-option label="月度资产负债表 (Monthly Balance Sheet)" value="monthly_asset_audit" />
          </el-select>
        </el-form-item>

        <el-row :gutter="16">
          <el-col :span="12">
            <div class="switch-card">
              <div class="switch-info">
                <span class="switch-title">包含持仓明细清单</span>
                <span class="switch-desc">按仓位排序罗列前十大资产头寸与盈亏比率</span>
              </div>
              <el-switch v-model="form.include_positions" />
            </div>
          </el-col>
          <el-col :span="12">
            <div class="switch-card">
              <div class="switch-info">
                <span class="switch-title">包含近 7 日收支记录</span>
                <span class="switch-desc">附加统计周期内的现金流出入变动</span>
              </div>
              <el-switch v-model="form.include_cashflow" />
            </div>
          </el-col>
        </el-row>
      </template>

      <!-- Delivery Config -->
      <div class="form-section-title">结果推送与通知</div>
      <el-form-item label="推送目标渠道" prop="delivery_channel">
        <el-radio-group v-model="form.delivery_channel" class="delivery-group">
          <el-radio-button value="user_default">
            <span class="radio-label">
              <Icon icon="ph:wechat-logo-bold" class="radio-icon" />
              个人默认微信通道 (推荐)
            </span>
          </el-radio-button>
          <el-radio-button value="silent_db">
            <span class="radio-label">
              <Icon icon="ph:archive-box-bold" class="radio-icon" />
              静默归档 (仅记日志不推微信)
            </span>
          </el-radio-button>
          <el-radio-button value="custom_webhook">
            <span class="radio-label">
              <Icon icon="ph:link-bold" class="radio-icon" />
              独立 Webhook
            </span>
          </el-radio-button>
        </el-radio-group>
      </el-form-item>

      <template v-if="form.delivery_channel === 'custom_webhook'">
        <el-form-item label="自定义 Webhook 接收地址" prop="custom_webhook_url">
          <el-input
            v-model="form.custom_webhook_url"
            placeholder="https://your-custom-webhook-endpoint..."
            clearable
          />
        </el-form-item>
      </template>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button
          type="primary"
          :loading="saving"
          @click="handleSave"
        >
          {{ isEdit ? '保存修改' : '立即创建' }}
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import { schedulerApi } from '@/api/scheduler'
import type { ScheduledJob, JobType, TriggerType, DeliveryChannel } from '@/types'

const emit = defineEmits<{
  (e: 'saved'): void
}>()

const visible = ref(false)
const saving = ref(false)
const editingJobId = ref<number | null>(null)
const activeTab = ref<JobType>('ai_prompt')
const cronPreset = ref('0 8 * * *')

const isEdit = computed(() => editingJobId.value !== null)

const formRef = ref<FormInstance>()

const form = reactive({
  name: '',
  description: '',
  trigger_type: 'cron' as TriggerType,
  cron_expr: '0 8 * * *',
  event_name: 'daily_summary_event',
  ai_prompt: '分析当前投资组合的集中度与风险暴露，结合近期市场大盘走势，生成一份精炼的投资早报。',
  with_holdings: true,
  with_web_search: false,
  report_type: 'daily_closing_report',
  include_positions: true,
  include_cashflow: true,
  delivery_channel: 'user_default' as DeliveryChannel,
  custom_webhook_url: ''
})

const rules: FormRules = {
  name: [{ required: true, message: '请输入任务名称', trigger: 'blur' }],
  cron_expr: [
    {
      validator: (_rule, value, callback) => {
        if (form.trigger_type === 'cron') {
          if (!value || !value.trim()) {
            return callback(new Error('请输入 Cron 表达式'))
          }
          const parts = value.trim().split(/\s+/)
          if (parts.length !== 5) {
            return callback(new Error('Cron 表达式须为标准的 5 段格式 (分 时 日 月 周)'))
          }
        }
        callback()
      },
      trigger: 'blur'
    }
  ],
  ai_prompt: [
    {
      validator: (_rule, value, callback) => {
        if (activeTab.value === 'ai_prompt') {
          if (!value || !value.trim()) {
            return callback(new Error('请输入 AI 任务提示词指令'))
          }
        }
        callback()
      },
      trigger: 'blur'
    }
  ],
  custom_webhook_url: [
    {
      validator: (_rule, value, callback) => {
        if (form.delivery_channel === 'custom_webhook') {
          if (!value || !value.trim()) {
            return callback(new Error('请输入 Webhook 接收地址'))
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

const applyCronPreset = (val: string | number | boolean | undefined) => {
  if (val && val !== 'custom') {
    form.cron_expr = String(val)
  }
}

const onCronInput = () => {
  const presets = ['0 8 * * *', '0 18 * * 1-5', '0 21 * * *', '0 9 * * 1']
  if (!presets.includes(form.cron_expr.trim())) {
    cronPreset.value = 'custom'
  } else {
    cronPreset.value = form.cron_expr.trim()
  }
}

const handleTabChange = () => {
  if (isEdit.value) {
    ElMessage.info('编辑模式下不支持切换任务核心类型')
    return false
  }
  return true
}

const open = (job?: ScheduledJob) => {
  visible.value = true
  if (job) {
    editingJobId.value = job.id
    activeTab.value = job.job_type
    form.name = job.name
    form.description = job.description || ''
    form.trigger_type = job.trigger_type
    form.delivery_channel = job.delivery_channel

    // Parse trigger_config
    if (job.trigger_type === 'cron') {
      try {
        const trig = JSON.parse(job.trigger_config || '{}')
        form.cron_expr = trig.cron_expr || '0 8 * * *'
      } catch {
        form.cron_expr = '0 8 * * *'
      }
      onCronInput()
    } else {
      try {
        const trig = JSON.parse(job.trigger_config || '{}')
        form.event_name = trig.event_name || 'daily_summary_event'
      } catch {
        form.event_name = 'daily_summary_event'
      }
    }

    // Parse payload_config
    if (job.job_type === 'ai_prompt') {
      try {
        const payload = JSON.parse(job.payload_config || '{}')
        form.ai_prompt = payload.prompt || ''
        form.with_holdings = payload.with_holdings !== false
        form.with_web_search = !!payload.with_web_search
      } catch {
        form.ai_prompt = ''
        form.with_holdings = true
        form.with_web_search = false
      }
    } else {
      try {
        const payload = JSON.parse(job.payload_config || '{}')
        form.report_type = payload.report_type || 'daily_closing_report'
        form.include_positions = payload.include_positions !== false
        form.include_cashflow = payload.include_cashflow !== false
      } catch {
        form.report_type = 'daily_closing_report'
        form.include_positions = true
        form.include_cashflow = true
      }
    }

    // Parse delivery_config
    if (job.delivery_channel === 'custom_webhook') {
      try {
        const deliv = JSON.parse(job.delivery_config || '{}')
        form.custom_webhook_url = deliv.webhook_url || ''
      } catch {
        form.custom_webhook_url = ''
      }
    }
  } else {
    editingJobId.value = null
    activeTab.value = 'ai_prompt'
    form.name = ''
    form.description = ''
    form.trigger_type = 'cron'
    form.cron_expr = '0 8 * * *'
    cronPreset.value = '0 8 * * *'
    form.event_name = 'daily_summary_event'
    form.ai_prompt = '分析当前投资组合的集中度与风险暴露，结合近期市场大盘走势，生成一份精炼的投资早报。'
    form.with_holdings = true
    form.with_web_search = false
    form.report_type = 'daily_closing_report'
    form.include_positions = true
    form.include_cashflow = true
    form.delivery_channel = 'user_default'
    form.custom_webhook_url = ''
  }
}

const handleSave = async () => {
  if (!formRef.value) return
  await formRef.value.validate()

  const triggerConfigObj =
    form.trigger_type === 'cron'
      ? { cron_expr: form.cron_expr.trim() }
      : { event_name: form.event_name }

  const payloadConfigObj =
    activeTab.value === 'ai_prompt'
      ? {
          prompt: form.ai_prompt.trim(),
          with_holdings: form.with_holdings,
          with_web_search: form.with_web_search
        }
      : {
          report_type: form.report_type,
          include_positions: form.include_positions,
          include_cashflow: form.include_cashflow
        }

  const deliveryConfigObj =
    form.delivery_channel === 'custom_webhook'
      ? { webhook_url: form.custom_webhook_url.trim() }
      : {}

  saving.value = true
  try {
    if (isEdit.value && editingJobId.value) {
      await schedulerApi.updateJob(editingJobId.value, {
        name: form.name.trim(),
        description: form.description.trim(),
        trigger_type: form.trigger_type,
        trigger_config: JSON.stringify(triggerConfigObj),
        payload_config: JSON.stringify(payloadConfigObj),
        delivery_channel: form.delivery_channel,
        delivery_config: JSON.stringify(deliveryConfigObj)
      })
      ElMessage.success('定时任务更新成功')
    } else {
      await schedulerApi.createJob({
        name: form.name.trim(),
        description: form.description.trim(),
        job_type: activeTab.value,
        trigger_type: form.trigger_type,
        trigger_config: JSON.stringify(triggerConfigObj),
        payload_config: JSON.stringify(payloadConfigObj),
        delivery_channel: form.delivery_channel,
        delivery_config: JSON.stringify(deliveryConfigObj)
      })
      ElMessage.success('定时任务创建成功')
    }
    visible.value = false
    emit('saved')
  } catch (err: any) {
    ElMessage.error(err.message || '保存任务失败')
  } finally {
    saving.value = false
  }
}

defineExpose({
  open
})
</script>

<style scoped>
.job-mode-tabs {
  margin-bottom: 20px;
}

.tab-label {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  font-weight: 500;
}

.tab-icon {
  font-size: 18px;
}

.ai-icon {
  color: #8b5cf6;
}

.report-icon {
  color: #3b82f6;
}

.form-section-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  margin: 18px 0 12px;
  padding-left: 8px;
  border-left: 3px solid var(--el-color-primary);
}

.field-tip {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: 6px;
  line-height: 1.5;
}

.field-tip code {
  background: var(--el-fill-color-light);
  padding: 2px 4px;
  border-radius: 4px;
}

.switch-card {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 14px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
  border: 1px solid var(--el-border-color-lighter);
}

.switch-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.switch-title {
  font-size: 13px;
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.switch-desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.delivery-group {
  display: flex;
  width: 100%;
}

.delivery-group :deep(.el-radio-button) {
  flex: 1;
}

.delivery-group :deep(.el-radio-button__inner) {
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

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}
</style>
