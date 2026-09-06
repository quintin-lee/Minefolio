<template>
  <div class="panel-container">
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
              <el-button text size="small" @click="toggleEdit(index)" :aria-label="t('common.edit')">
                <el-icon><Edit /></el-icon>
              </el-button>
              <el-button text size="small" class="delete-btn" @click="removeProvider(index)" :aria-label="t('common.delete')">
                <el-icon><Delete /></el-icon>
              </el-button>
          </div>
        </div>
        <el-form v-if="editIndex === index" :model="provider" label-width="100px" class="provider-form">
          <el-form-item :label="t('settings.aiProviderId')">
            <el-input v-model="provider.id" :disabled="provider.id.length > 0 && provider.has_api_key" :placeholder="t('settings.aiProviderIdPlaceholder')" />
          </el-form-item>
          <el-form-item :label="t('settings.aiProviderName')">
            <el-input v-model="provider.name" :placeholder="t('settings.aiProviderNamePlaceholder')" />
          </el-form-item>
          <el-form-item :label="t('settings.aiBaseUrl')">
            <el-input v-model="provider.base_url" :placeholder="t('settings.aiBaseUrlPlaceholder')" />
          </el-form-item>
          <el-form-item :label="t('settings.aiApiKey')">
            <el-input
              v-model="provider.api_key"
              type="password"
              show-password
              :placeholder="provider.has_api_key ? t('settings.aiApiKeyMaskedPlaceholder') : t('settings.aiApiKeyEmptyPlaceholder')"
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
                  {{ t('settings.aiFetchModels') }}
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
                  {{ t('settings.aiTestConnection') }}
                </el-button>
              </div>
              <div class="right-actions">
                <el-button type="primary" size="small" @click="saveProvider(index)">{{ t('settings.aiOk') }}</el-button>
                <el-button size="small" @click="cancelEdit">{{ t('common.cancel') }}</el-button>
              </div>
            </div>
          </el-form-item>
        </el-form>
        <div v-else class="provider-details">
          <el-tag size="small" class="meta-tag">ID: {{ provider.id }}</el-tag>
          <el-tag size="small" class="meta-tag" v-if="provider.modelsStr">{{ t('settings.aiModelTag', { models: provider.modelsStr }) }}</el-tag>
          <el-tag size="small" :type="(provider.has_api_key || (provider.api_key && provider.api_key.trim())) ? 'success' : 'info'" class="meta-tag">
            {{ (provider.has_api_key || (provider.api_key && provider.api_key.trim())) ? t('settings.aiApiKeyConfigured') : t('settings.aiApiKeyNotSet') }}
          </el-tag>
          <el-button
            size="small"
            text
            class="card-test-btn"
            :loading="testingIndex === index"
            @click.stop="handleTestConnection(index)"
          >
            <Icon icon="ph:plug" style="margin-right: 2px" />
            {{ t('settings.aiTestConnection') }}
          </el-button>
        </div>
      </div>
      <el-button type="primary" plain @click="addProvider" class="add-provider-btn" :aria-label="t('settings.aiAddProvider')">
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
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import { Edit, Delete, Plus } from '@element-plus/icons-vue'
import { getSettings, updateSettings, testAiConnection, fetchAiModels } from '@/api/ai'
import type { AiSettings, AiTestConnectionResult } from '@/api/ai'
import { encryptText } from '@/utils/crypto'
import { t } from '@/utils/locale'

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
const aiSaving = ref(false)
const editIndex = ref(-1)

const aiForm = reactive({
  default_provider: '',
  default_model: '',
  context_size: 20,
  system_prompt: '',
})

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
  editIndex.value = editIndex.value === index ? -1 : index
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
    ElMessage.error(t('settings.aiProviderIdRequired'))
    return
  }
  if (provider.api_key && provider.api_key.trim()) {
    provider.has_api_key = true
  }
  editIndex.value = -1
  ElMessage.success(t('settings.aiStagedHint'))
}

async function handleTestConnection(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning(t('settings.aiProviderIdFillFirst'))
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
      if (p.api_key && p.api_key.trim()) {
        p.has_api_key = true
      }
      ElMessage.success(t('settings.aiTestSuccess', { ms: res.latency_ms }))
    } else {
      ElMessage.error(t('settings.aiTestFailedDetail', { message: res.message }))
    }
  } catch {
    p.testResult = { success: false, latency_ms: 0, message: t('settings.aiRequestFailed') }
    ElMessage.error(t('settings.aiTestError'))
  } finally {
    testingIndex.value = null
  }
}

async function handleFetchModels(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning(t('settings.aiProviderIdFillFirst'))
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
      if (p.api_key && p.api_key.trim()) {
        p.has_api_key = true
      }
      ElMessage.success(t('settings.aiFetchSuccess', { n: models.length }))
    } else {
      ElMessage.warning(t('settings.aiFetchEmpty'))
    }
  } catch {
    ElMessage.error(t('settings.aiFetchFailed'))
  } finally {
    fetchingModelsIndex.value = null
  }
}

async function removeProvider(index: number) {
  try {
    await ElMessageBox.confirm(t('settings.aiConfirmDelete'), t('common.hint'), { type: 'warning' })
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
    ElMessage.error(t('settings.saveFailed'))
  } finally {
    aiSaving.value = false
  }
}

loadAiSettings()
</script>

<style scoped>
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
