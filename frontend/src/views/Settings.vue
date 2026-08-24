<template>
  <div class="settings-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>{{ t('settings.title') }}</h2>
      </div>
    </div>
    <div class="settings-content">
      <el-row :gutter="24" class="info-cards">
        <el-col :span="24">
          <div class="panel-container">
            <div class="panel-header">
              <h3>{{ t('settings.userInfo') }}</h3>
            </div>
            <div class="info-row">
              <span class="info-label">{{ t('settings.username') }}</span>
              <span class="info-value">{{ auth.user?.username || '-' }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">{{ t('settings.accountId') }}</span>
              <span class="info-value mono-text">{{ auth.user?.id || '-' }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">{{ t('settings.registeredAt') }}</span>
              <span class="info-value">{{ formatDate(auth.user?.created_at || '') }}</span>
            </div>
          </div>
        </el-col>
      </el-row>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.changePassword') }}</h3>
        </div>
        <el-form :model="form" :rules="rules" ref="formRef" label-width="120px" class="premium-form" @keyup.enter="submit">
          <el-form-item :label="t('settings.oldPassword')" prop="old_password">
            <el-input v-model="form.old_password" type="password" show-password :placeholder="t('settings.oldPassword')" />
          </el-form-item>
          <el-form-item :label="t('settings.newPassword')" prop="new_password">
            <el-input v-model="form.new_password" type="password" show-password :placeholder="t('settings.newPassword')" />
          </el-form-item>
          <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
            <el-input v-model="form.confirmPassword" type="password" show-password :placeholder="t('settings.confirmPassword')" />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" class="action-btn" @click="submit" :loading="loading">
              {{ t('settings.savePassword') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.exportData') }}</h3>
        </div>
        <p class="export-hint">{{ t('settings.exportHint') }}</p>
        <el-button type="primary" class="action-btn" @click="handleExport" :loading="exporting">
          {{ t('settings.exportButton') }}
        </el-button>
      </div>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.aiTitle') }}</h3>
        </div>
        <p class="export-hint">{{ t('settings.aiDesc') }}</p>
        
        <div class="provider-list">
          <div v-for="(provider, index) in providerList" :key="index" class="provider-item">
            <div class="provider-header">
              <div class="provider-title-wrap">
                <span class="provider-name">{{ provider.name || provider.id }}</span>
                <el-tag size="small" :type="provider.testResult?.success ? 'success' : 'danger'" v-if="provider.testResult">
                  {{ provider.testResult.success ? `${provider.testResult.latency_ms}ms` : provider.testResult.message }}
                </el-tag>
              </div>
              <div class="provider-actions">
                <el-button text size="small" @click="toggleEdit(index)" title="编辑">
                  <el-icon><Edit /></el-icon>
                </el-button>
                <el-button text size="small" class="delete-btn" @click="removeProvider(index)" title="删除">
                  <el-icon><Delete /></el-icon>
                </el-button>
              </div>
            </div>
            <el-form v-if="editIndex === index" :model="provider" label-width="100px" class="provider-form">
              <el-form-item :label="t('settings.aiProviderId')">
                <el-input v-model="provider.id" :disabled="provider.id.length > 0 && provider.has_api_key" placeholder="例如: qwen, openai, deepseek" />
              </el-form-item>
              <el-form-item :label="t('settings.aiProviderName')">
                <el-input v-model="provider.name" placeholder="显示名称 (如: 通义千问 27B)" />
              </el-form-item>
              <el-form-item :label="t('settings.aiBaseUrl')">
                <el-input v-model="provider.base_url" placeholder="https://api.openai.com/v1 或 http://host:port/v1" />
              </el-form-item>
              <el-form-item :label="t('settings.aiApiKey')">
                <el-input
                  v-model="provider.api_key"
                  type="password"
                  show-password
                  :placeholder="provider.has_api_key ? '•••••••• (已加密存储，留空保持不变)' : 'sk-...'"
                />
              </el-form-item>
              <el-form-item :label="t('settings.aiModels')">
                <div class="models-input-wrap">
                  <el-input v-model="provider.modelsStr" type="textarea" :rows="2" :placeholder="t('settings.aiModelPlaceholder')" />
                  <div class="models-actions">
                    <el-button
                      size="small"
                      :loading="fetchingModelsIndex === index"
                      @click="handleFetchModels(index)"
                    >
                      <Icon icon="ph:cloud-arrow-down-bold" style="margin-right: 4px" />
                      自动拉取模型
                    </el-button>
                  </div>
                </div>
              </el-form-item>
              <el-form-item>
                <div class="form-actions-bar">
                  <div class="left-actions">
                    <el-button
                      size="small"
                      :loading="testingIndex === index"
                      @click="handleTestConnection(index)"
                    >
                      <Icon icon="ph:plug-bold" style="margin-right: 4px" />
                      测试连接
                    </el-button>
                  </div>
                  <div class="right-actions">
                    <el-button type="primary" size="small" @click="saveProvider(index)">确定</el-button>
                    <el-button size="small" @click="cancelEdit">取消</el-button>
                  </div>
                </div>
              </el-form-item>
            </el-form>
            <div v-else class="provider-details">
              <el-tag size="small" class="meta-tag">ID: {{ provider.id }}</el-tag>
              <el-tag size="small" class="meta-tag" v-if="provider.modelsStr">模型: {{ provider.modelsStr }}</el-tag>
              <el-tag size="small" :type="provider.has_api_key ? 'success' : 'info'" class="meta-tag">
                {{ provider.has_api_key ? 'API Key 已配置 (传输加密)' : '未设置 API Key' }}
              </el-tag>
              <el-button
                size="small"
                text
                class="card-test-btn"
                :loading="testingIndex === index"
                @click.stop="handleTestConnection(index)"
              >
                <Icon icon="ph:plug" style="margin-right: 2px" />
                测试连接
              </el-button>
            </div>
          </div>
          <el-button type="primary" plain @click="addProvider" class="add-provider-btn">
            <el-icon><Plus /></el-icon>
            {{ t('settings.aiAddProvider') }}
          </el-button>
        </div>
        
        <el-divider />
        
        <el-form :model="aiForm" label-width="120px" class="premium-form">
          <el-form-item :label="t('settings.aiDefaultProvider')">
            <el-select v-model="aiForm.default_provider" style="width: 100%">
              <el-option
                v-for="p in providerList"
                :key="p.id"
                :label="p.name || p.id"
                :value="p.id"
              />
            </el-select>
          </el-form-item>
          <el-form-item :label="t('settings.aiDefaultModel')">
            <el-select v-model="aiForm.default_model" style="width: 100%">
              <el-option
                v-for="m in allModels"
                :key="m"
                :label="m"
                :value="m"
              />
            </el-select>
          </el-form-item>
          <el-form-item :label="t('settings.aiContextSize')">
            <el-input-number v-model="aiForm.context_size" :min="5" :max="100" :step="5" />
          </el-form-item>
          <el-form-item :label="t('settings.aiSystemPrompt')">
            <el-input v-model="aiForm.system_prompt" type="textarea" :rows="4" :maxlength="500" show-word-limit />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="saveAiSettings" :loading="aiSaving">
              {{ aiSaving ? t('settings.aiSaving') : t('settings.aiSave') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { transactionsApi } from '@/api/transactions'
import { getSettings, updateSettings, testAiConnection, fetchAiModels } from '@/api/ai'
import type { AiSettings, AiTestConnectionResult } from '@/api/ai'
import { encryptText } from '@/utils/crypto'
import { Icon } from '@iconify/vue'
import { zhCN } from '@/locales/zh-CN'
import { formatDate } from '@/utils/format'
import type { FormInstance, FormRules } from 'element-plus'

const t = (key: string): string => {
  const keys = key.split('.')
  let obj: unknown = zhCN
  for (const k of keys) {
    if (obj && typeof obj === 'object' && k in obj) {
      obj = (obj as Record<string, unknown>)[k]
    } else {
      return key
    }
  }
  return typeof obj === 'string' ? obj : key
}

const auth = useAuthStore()
const loading = ref(false)
const exporting = ref(false)
const formRef = ref<FormInstance>()
const aiSaving = ref(false)
const editIndex = ref(-1)

interface ProviderItem {
  id: string
  name: string
  base_url: string
  api_key?: string
  has_api_key?: boolean
  modelsStr: string
  testResult?: AiTestConnectionResult
}
const providerList = ref<ProviderItem[]>([])
const testingIndex = ref<number | null>(null)
const fetchingModelsIndex = ref<number | null>(null)
const aiForm = reactive({
  default_provider: '',
  default_model: '',
  context_size: 20,
  system_prompt: '',
})

const form = reactive({
  old_password: '',
  new_password: '',
  confirmPassword: '',
})

const validateConfirm = (rule: any, value: string, callback: any) => {
  if (value !== form.new_password) {
    callback(new Error(t('settings.passwordMismatch')))
  } else {
    callback()
  }
}

const rules: FormRules = {
  old_password: [{ required: true, message: t('settings.oldPassword'), trigger: 'blur' }],
  new_password: [
    { required: true, message: t('settings.newPassword'), trigger: 'blur' },
    { min: 6, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: t('settings.confirmPassword'), trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' },
  ],
}

const allModels = computed(() => {
  const models = new Set<string>()
  providerList.value.forEach(p => {
    p.modelsStr.split(',').map(s => s.trim()).filter(Boolean).forEach(m => models.add(m))
  })
  return Array.from(models)
})

async function loadAiSettings() {
  try {
    const raw = await getSettings()
    const settings = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: AiSettings }).data : raw) as AiSettings
    if (settings) {
      aiForm.default_provider = settings.default_provider || ''
      aiForm.default_model = settings.default_model || ''
      aiForm.context_size = settings.context_size || 20
      aiForm.system_prompt = settings.system_prompt || ''
      
      providerList.value = (settings.providers ?? []).map((p) => ({
        id: p.id || '',
        name: p.name || p.id || '',
        base_url: p.base_url || '',
        api_key: '',
        has_api_key: !!p.has_api_key,
        modelsStr: Array.isArray(p.models) ? p.models.join(', ') : '',
      }))
    }
  } catch {
    // Load failed silently
  }
}

function toggleEdit(index: number) {
  if (editIndex.value === index) {
    editIndex.value = -1
  } else {
    editIndex.value = index
  }
}

function cancelEdit() {
  editIndex.value = -1
}

function addProvider() {
  providerList.value.push({
    id: '',
    name: '',
    base_url: '',
    api_key: '',
    modelsStr: '',
  })
  editIndex.value = providerList.value.length - 1
}

function saveProvider(index: number) {
  const provider = providerList.value[index]
  if (!provider || !provider.id) {
    ElMessage.error('供应商 ID 不能为空')
    return
  }
  editIndex.value = -1
  ElMessage.success(t('settings.aiProviderAdded'))
}


async function handleTestConnection(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning('请先填写供应商 ID')
    return
  }
  testingIndex.value = index
  try {
    let api_key_enc: string | undefined = undefined
    if (p.api_key && p.api_key.trim()) {
      api_key_enc = await encryptText(p.api_key.trim())
    }
    const res = await testAiConnection({
      id: p.id,
      base_url: p.base_url,
      api_key_enc,
      model: p.modelsStr.split(',')[0]?.trim() || undefined,
    })
    p.testResult = res
    if (res.success) {
      ElMessage.success(`连接测试成功 (耗时 ${res.latency_ms}ms)`)
    } else {
      ElMessage.error(`连接测试失败: ${res.message}`)
    }
  } catch {
    p.testResult = { success: false, latency_ms: 0, message: '请求失败' }
    ElMessage.error('连接测试异常')
  } finally {
    testingIndex.value = null
  }
}

async function handleFetchModels(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning('请先填写供应商 ID')
    return
  }
  fetchingModelsIndex.value = index
  try {
    let api_key_enc: string | undefined = undefined
    if (p.api_key && p.api_key.trim()) {
      api_key_enc = await encryptText(p.api_key.trim())
    }
    const models = await fetchAiModels({
      id: p.id,
      base_url: p.base_url,
      api_key_enc,
    })
    if (models && models.length > 0) {
      p.modelsStr = models.join(', ')
      ElMessage.success(`成功拉取 ${models.length} 个可用模型`)
    } else {
      ElMessage.warning('未获取到模型列表，请手动输入')
    }
  } catch {
    ElMessage.error('获取模型列表失败')
  } finally {
    fetchingModelsIndex.value = null
  }
}
async function removeProvider(index: number) {
  try {
    await ElMessageBox.confirm(t('settings.aiConfirmDelete'), '提示', { type: 'warning' })
    providerList.value.splice(index, 1)
    ElMessage.success(t('settings.aiProviderDeleted'))
  } catch {
    // Cancelled
  }
}

async function saveAiSettings() {
  aiSaving.value = true
  try {
    const providers = await Promise.all(
      providerList.value.map(async (p) => {
        let api_key_enc: string | undefined = undefined
        if (p.api_key && p.api_key.trim()) {
          api_key_enc = await encryptText(p.api_key.trim())
        }
        return {
          id: p.id,
          name: p.name,
          base_url: p.base_url,
          api_key_enc,
          api_key: api_key_enc,
          models: p.modelsStr.split(',').map(s => s.trim()).filter(Boolean),
        }
      })
    )
    
    await updateSettings({
      ...aiForm,
      providers,
    })
    ElMessage.success(t('settings.aiSaved'))
    await loadAiSettings()
  } catch {
    ElMessage.error(t('settings.aiSaveFailed') || '保存失败')
  } finally {
    aiSaving.value = false
  }
}

async function submit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    try {
      loading.value = true
      await auth.changePassword(form.old_password, form.new_password)
      ElMessage.success(t('settings.passwordSuccess'))
      form.old_password = ''
      form.new_password = ''
      form.confirmPassword = ''
    } catch {
      // error handled by http interceptor
    } finally {
      loading.value = false
    }
  })
}

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

onMounted(() => {
  loadAiSettings()
})
</script>

<style scoped>
.settings-page {
  background-color: var(--mf-background);
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-primary);
  flex-shrink: 0;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #6366f1, #8b5cf6);
  border-radius: 2px;
}

.header-title h2 {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
}

.settings-content {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.info-cards {
  margin-bottom: 0;
}

.panel-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.panel-header {
  margin-bottom: 20px;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border);
}

.info-row:last-child {
  border-bottom: none;
}

.info-label {
  color: var(--mf-text-muted);
  font-size: 14px;
}

.info-value {
  color: var(--mf-text-main);
  font-weight: 500;
  font-size: 14px;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.6) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.action-btn {
  width: 160px;
}

.export-hint {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 16px;
  line-height: 1.6;
}

.provider-list {
  margin-bottom: 24px;
}

.provider-item {
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 12px;
  margin-bottom: 12px;
  background: var(--mf-surface);
}

.provider-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.provider-name {
  font-weight: 500;
  color: var(--mf-text-main);
  font-size: 14px;
}

.provider-actions {
  display: flex;
  gap: 4px;
}

.delete-btn:hover {
  color: var(--color-danger);
}

.provider-details {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.meta-tag {
  font-size: 12px;
}

.provider-form {
  margin-top: 8px;
}

.add-provider-btn {
  width: 100%;
  margin-top: 8px;
}

.provider-title-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.models-input-wrap {
  width: 100%;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.models-actions {
  display: flex;
  justify-content: flex-end;
}

.form-actions-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
}

.card-test-btn {
  font-size: 12px;
  color: var(--mf-primary);
  margin-left: auto;
}

:deep(.el-divider) {
  margin: 24px 0;
}
</style>
