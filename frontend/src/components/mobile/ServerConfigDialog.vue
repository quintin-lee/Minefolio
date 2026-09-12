<template>
  <el-dialog
    v-model="visible"
    title="服务端地址设置"
    width="90%"
    class="server-config-dialog"
    :close-on-click-modal="!testing && !saving"
    :append-to-body="true"
    @open="handleOpen"
  >
    <div class="server-config-content">
      <p class="dialog-desc">
        配置移动端连接的 Minefolio 后端服务接口地址（支持局域网 IP 或公网域名）。
      </p>

      <el-form label-position="top">
        <el-form-item label="服务端接口地址">
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
        </el-form-item>
      </el-form>

      <div class="dialog-tips">
        <div class="tip-item">局域网自建：http://192.168.x.x:8080</div>
        <div class="tip-item">公网反向代理：https://minefolio.example.com</div>
        <div class="tip-item">留空恢复系统默认地址</div>
      </div>

      <div
        v-if="testResult"
        class="test-result-box"
        :class="{ 'is-ok': testResult.ok, 'is-err': !testResult.ok }"
      >
        <el-icon class="test-icon">
          <component :is="testResult.ok ? SuccessFilled : CircleCloseFilled" />
        </el-icon>
        <div class="test-msg">{{ testResult.message }}</div>
      </div>
    </div>

    <template #footer>
      <div class="dialog-footer-actions">
        <el-button
          :loading="testing"
          :disabled="saving"
          @click="handleTest"
        >
          测试连接
        </el-button>
        <el-button
          v-if="isCustom"
          type="info"
          plain
          :disabled="testing || saving"
          @click="handleReset"
        >
          恢复默认
        </el-button>
        <el-button
          type="primary"
          :loading="saving"
          :disabled="testing"
          @click="handleSave"
        >
          保存并应用
        </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Connection, SuccessFilled, CircleCloseFilled } from '@element-plus/icons-vue'
import { useServerUrl, type ServerConnectionTestResult } from '@/composables/useServerUrl'

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', val: boolean): void
  (e: 'saved', newUrl: string): void
}>()

const visible = computed({
  get: () => props.modelValue,
  set: (val: boolean) => emit('update:modelValue', val),
})

const { serverUrl, isCustom, setUrl, resetUrl, testConnection, isValidServerUrl, normalizeServerUrl } = useServerUrl()

const inputUrl = ref('')
const testing = ref(false)
const saving = ref(false)
const testResult = ref<ServerConnectionTestResult | null>(null)

function handleOpen() {
  inputUrl.value = serverUrl.value
  testResult.value = null
}

watch(() => props.modelValue, (open) => {
  if (open) {
    handleOpen()
  }
})

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

  saving.value = true
  try {
    const normalized = normalizeServerUrl(target)
    setUrl(normalized)
    ElMessage.success('服务端地址已保存并应用')
    emit('saved', normalized)
    visible.value = false
  } finally {
    saving.value = false
  }
}

function handleReset() {
  resetUrl()
  inputUrl.value = ''
  testResult.value = null
  ElMessage.info('已恢复为默认服务端地址')
  emit('saved', '')
  visible.value = false
}
</script>

<style scoped>
:deep(.server-config-dialog) {
  border-radius: 16px;
  max-width: 440px;
}

.server-config-content {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.dialog-desc {
  font-size: 13px;
  color: var(--mf-text-muted);
  line-height: 1.5;
  margin: 0;
}

.dialog-tips {
  background: var(--mf-surface-muted);
  border-radius: 8px;
  padding: 10px 12px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.tip-item {
  font-size: 12px;
  color: var(--mf-text-muted);
  font-family: monospace;
}

.test-result-box {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 13px;
  margin-top: 4px;
}

.test-result-box.is-ok {
  background: var(--mf-success-light);
  border: 1px solid var(--mf-success-border);
  color: var(--mf-success);
}

.test-result-box.is-err {
  background: var(--mf-danger-light);
  border: 1px solid var(--mf-danger-border);
  color: var(--mf-danger);
}

.test-icon {
  font-size: 16px;
  flex-shrink: 0;
}

.test-msg {
  flex: 1;
  word-break: break-all;
}

.dialog-footer-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  flex-wrap: wrap;
}
</style>
