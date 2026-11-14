<template>
  <el-dialog
    v-model="visible"
    :title="isEdit ? '编辑 MCP 工具服务' : '新增 MCP 工具服务'"
    width="680px"
    destroy-on-close
    :close-on-click-modal="false"
  >
    <el-tabs v-model="activeTab" class="mcp-dialog-tabs">
      <!-- Tab 1: 基础配置 -->
      <el-tab-pane label="基本配置" name="basic">
        <!-- 预置模板快速选择 (仅新建时展示) -->
        <div v-if="!isEdit" class="preset-selector-row">
          <span class="preset-label">快捷填充模板:</span>
          <el-select
            v-model="selectedPreset"
            placeholder="选择官方推荐预置模板快速填入配置..."
            clearable
            style="flex: 1"
            @change="handleApplyPreset"
          >
            <el-option
              v-for="p in presetTemplates"
              :key="p.value"
              :label="p.label"
              :value="p.value"
            />
          </el-select>
        </div>

        <el-form ref="formRef" :model="form" :rules="rules" label-position="top" class="mcp-form">
          <div class="form-row-2">
            <el-form-item label="服务唯一标识 (Name)" prop="name">
              <el-input
                v-model="form.name"
                placeholder="例如: sqlite-tools"
                :disabled="isEdit"
              />
              <div class="field-hint">小写字母、数字及横杠下划线 (^[a-z0-9_-]{1,64}$)</div>
            </el-form-item>

            <el-form-item label="传输协议类型 (Transport)" prop="transport">
              <el-radio-group v-model="form.transport" size="default">
                <el-radio-button label="stdio">Stdio 命令行</el-radio-button>
                <el-radio-button label="http">HTTP/SSE 远端</el-radio-button>
              </el-radio-group>
            </el-form-item>
          </div>

          <!-- Stdio 模式字段 -->
          <template v-if="form.transport === 'stdio'">
            <el-form-item label="启动命令 (Command)" prop="command">
              <el-input
                v-model="form.command"
                placeholder="例如: npx -y @modelcontextprotocol/server-filesystem /tmp 或 python3 script.py"
                clearable
              />
              <div class="field-hint">可执行程序或带参数的命令，系统将拉起独立进程进行 JSON-RPC 交互。</div>
            </el-form-item>

            <el-form-item label="启动参数 (JSON Array, 可选)">
              <el-input
                v-model="form.args"
                type="textarea"
                :rows="2"
                placeholder='["--verbose", "--db-path", "./data.db"]'
              />
              <div class="field-hint">必须为合法的 JSON 数组格式（如为空可留空）。</div>
            </el-form-item>

            <el-form-item label="环境变量 (JSON Object, 可选)">
              <el-input
                v-model="form.env"
                type="textarea"
                :rows="2"
                placeholder='{\n  "PYTHONUNBUFFERED": "1"\n}'
              />
              <div class="field-hint">必须为合法的 JSON 对象格式，将覆盖或注入至子进程环境中。</div>
            </el-form-item>
          </template>

          <!-- HTTP/SSE 模式字段 -->
          <template v-else>
            <el-form-item label="服务端点 URL" prop="url">
              <el-input
                v-model="form.url"
                placeholder="https://mcp.example.com/sse"
                clearable
              />
              <div class="field-hint">完整的 HTTP 或 SSE 服务端点，末尾不带斜杠。</div>
            </el-form-item>

            <el-form-item label="请求头 Headers (JSON Object, 可选)">
              <el-input
                v-model="form.headers"
                type="textarea"
                :rows="2"
                placeholder='{\n  "Authorization": "Bearer YOUR_TOKEN"\n}'
              />
            </el-form-item>

            <el-form-item label="凭据引用名 (Secret Ref, 可选)">
              <el-input
                v-model="form.secret_ref"
                placeholder="例如: OPENAI_API_KEY (存放在 Secret 仓库中)"
              />
            </el-form-item>
          </template>

          <div class="form-row-2">
            <el-form-item label="请求超时时间 (毫秒)">
              <el-input-number
                v-model="form.timeout_ms"
                :min="1000"
                :max="120000"
                :step="1000"
                style="width: 100%"
              />
            </el-form-item>

            <el-form-item label="启用状态">
              <el-switch
                v-model="formEnabled"
                active-text="启用 (AI可调度)"
                inactive-text="禁用"
              />
            </el-form-item>
          </div>
        </el-form>
      </el-tab-pane>

      <!-- Tab 2: 连通性测试 -->
      <el-tab-pane label="连通性与探活测试" name="test">
        <div class="test-pane-wrap">
          <el-alert
            type="info"
            :closable="false"
            show-icon
            class="test-alert"
          >
            <template #title>
              <span>测试 MCP 握手协议 (initialize + tools/list)</span>
            </template>
            系统将模拟客户端连接该服务并请求工具列表，验证传输层连通性并实时抓取可调用的工具清单。
          </el-alert>

          <div class="test-action-bar">
            <el-button
              type="primary"
              :loading="testing"
              @click="handleRunTest"
            >
              <Icon icon="ph:play-fill" class="btn-icon" />
              立即探活此配置
            </el-button>
          </div>

          <div v-if="testResult" class="test-result-box">
            <div class="result-header">
              <el-tag :type="testResult.success || testResult.status === 'ok' ? 'success' : 'danger'">
                {{ testResult.success || testResult.status === 'ok' ? '探活成功' : '探活失败' }}
              </el-tag>
              <span v-if="testResult.tools" class="result-metric">
                发现 {{ testResult.tools.length }} 个可用工具
              </span>
            </div>

            <div v-if="testResult.message && (!testResult.success && testResult.status !== 'ok')" class="error-msg">
              {{ testResult.message }}
            </div>

            <div v-if="testResult.tools && testResult.tools.length > 0" class="discovered-tools-list">
              <div
                v-for="t in testResult.tools"
                :key="t.tool_name"
                class="tool-item-preview"
              >
                <div class="tool-title-row">
                  <span class="tool-name">#{{ t.tool_name }}</span>
                </div>
                <div class="tool-desc">{{ t.description || '暂无工具描述' }}</div>
              </div>
            </div>
          </div>
        </div>
      </el-tab-pane>
    </el-tabs>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="handleSave">保存配置</el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage } from 'element-plus'
import type { FormInstance, FormRules } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { McpServer, McpServerInput, McpTestResult } from '@/types'
import { createMcpServer, updateMcpServer, testMcpServer } from '@/api/ai-mcp'

const emit = defineEmits<{
  (e: 'saved'): void
}>()

const visible = ref(false)
const isEdit = ref(false)
const currentServerId = ref(0)
const saving = ref(false)
const testing = ref(false)
const activeTab = ref('basic')
const selectedPreset = ref('')
const testResult = ref<McpTestResult | null>(null)

const formRef = ref<FormInstance>()

const form = reactive<McpServerInput>({
  name: '',
  transport: 'stdio',
  url: '',
  command: '',
  args: '',
  env: '',
  headers: '',
  secret_ref: '',
  timeout_ms: 30000,
  enabled: 1,
})

const formEnabled = computed({
  get: () => Number(form.enabled) === 1,
  set: (val: boolean) => {
    form.enabled = val ? 1 : 0
  },
})

const presetTemplates = [
  {
    value: 'npx-fs',
    label: '本地文件系统 (Npx Filesystem)',
    transport: 'stdio' as const,
    name: 'filesystem',
    command: 'npx -y @modelcontextprotocol/server-filesystem /tmp',
    args: '',
    env: '',
  },
  {
    value: 'uvx-fetch',
    label: '通用网页抓取 (Uvx Fetch Tool)',
    transport: 'stdio' as const,
    name: 'fetch',
    command: 'uvx mcp-server-fetch',
    args: '',
    env: '',
  },
  {
    value: 'uvx-sqlite',
    label: '本地数据库管理 (Uvx SQLite Tool)',
    transport: 'stdio' as const,
    name: 'sqlite',
    command: 'uvx mcp-server-sqlite --db-path ./data/minefolio.db',
    args: '',
    env: '',
  },
  {
    value: 'npx-memory',
    label: '知识图谱长期记忆 (Npx Memory Tool)',
    transport: 'stdio' as const,
    name: 'memory',
    command: 'npx -y @modelcontextprotocol/server-memory',
    args: '',
    env: '',
  },
  {
    value: 'custom-py',
    label: '自定义 Python 脚本工具服务',
    transport: 'stdio' as const,
    name: 'python-tools',
    command: 'python3 scripts/my_tool.py',
    args: '[]',
    env: '{\n  "PYTHONUNBUFFERED": "1"\n}',
  },
]

function handleApplyPreset(val: string) {
  if (!val) return
  const tmpl = presetTemplates.find(p => p.value === val)
  if (!tmpl) return
  form.transport = tmpl.transport
  form.name = tmpl.name
  form.command = tmpl.command
  form.args = tmpl.args
  form.env = tmpl.env
}

const rules: FormRules = {
  name: [
    { required: true, message: '服务名称不能为空', trigger: 'blur' },
    {
      pattern: /^[a-z0-9_-]{1,64}$/,
      message: '仅支持 1-64 位小写字母、数字及 - _',
      trigger: 'blur',
    },
  ],
  command: [
    {
      validator: (_rule, value, callback) => {
        if (form.transport === 'stdio' && (!value || !value.trim())) {
          callback(new Error('stdio 模式下必须指定启动命令'))
        } else {
          callback()
        }
      },
      trigger: 'blur',
    },
  ],
  url: [
    {
      validator: (_rule, value, callback) => {
        if (form.transport === 'http' && (!value || !value.trim())) {
          callback(new Error('HTTP 模式下必须指定服务 URL'))
        } else {
          callback()
        }
      },
      trigger: 'blur',
    },
  ],
}

function openCreate() {
  isEdit.value = false
  currentServerId.value = 0
  activeTab.value = 'basic'
  selectedPreset.value = ''
  testResult.value = null

  form.name = ''
  form.transport = 'stdio'
  form.url = ''
  form.command = ''
  form.args = ''
  form.env = ''
  form.headers = ''
  form.secret_ref = ''
  form.timeout_ms = 30000
  form.enabled = 1

  visible.value = true
}

function openEdit(server: McpServer) {
  isEdit.value = true
  currentServerId.value = server.id
  activeTab.value = 'basic'
  selectedPreset.value = ''
  testResult.value = null

  form.name = server.name
  form.transport = server.transport
  form.url = server.url || ''
  form.command = server.command || ''
  form.args = server.args || ''
  form.env = server.env || ''
  form.headers = server.headers || ''
  form.secret_ref = server.secret_ref || ''
  form.timeout_ms = server.timeout_ms || 30000
  form.enabled = Number(server.enabled) === 1 ? 1 : 0

  visible.value = true
}

async function handleRunTest() {
  if (!isEdit.value) {
    ElMessage.info('新增配置请先保存后再进行实时探活')
    return
  }
  testing.value = true
  try {
    const res = await testMcpServer(currentServerId.value)
    testResult.value = res
    if (res.success || res.status === 'ok') {
      ElMessage.success(`探活成功，发现 ${res.tools?.length || 0} 个可用工具`)
    } else {
      ElMessage.warning(res.message || '探活返回异常')
    }
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    testResult.value = {
      success: false,
      status: 'error',
      message: e?.response?.data?.message || e?.message || '测试失败',
    }
  } finally {
    testing.value = false
  }
}

async function handleSave() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) return
    saving.value = true
    try {
      if (isEdit.value) {
        await updateMcpServer(currentServerId.value, form)
        ElMessage.success('MCP 服务更新成功')
      } else {
        await createMcpServer(form)
        ElMessage.success('MCP 服务创建成功')
      }
      visible.value = false
      emit('saved')
    } catch (err: unknown) {
      const e = err as { response?: { data?: { message?: string } }; message?: string }
      ElMessage.error(e?.response?.data?.message || e?.message || '保存失败')
    } finally {
      saving.value = false
    }
  })
}

defineExpose({
  openCreate,
  openEdit,
})
</script>

<style scoped>
.mcp-dialog-tabs {
  margin-top: -10px;
}

.preset-selector-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
  margin-bottom: 16px;
}

.preset-label {
  font-size: 13px;
  font-weight: 500;
  color: var(--el-text-color-regular);
  white-space: nowrap;
}

.mcp-form {
  padding-top: 6px;
}

.form-row-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.field-hint {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
  margin-top: 4px;
  line-height: 1.4;
}

.test-pane-wrap {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding-top: 8px;
}

.test-alert {
  border-radius: 8px;
}

.test-action-bar {
  display: flex;
  justify-content: flex-end;
}

.test-result-box {
  background: var(--el-fill-color-light);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 14px;
}

.result-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.result-metric {
  font-size: 13px;
  color: var(--el-text-color-regular);
}

.error-msg {
  color: var(--el-color-danger);
  font-size: 13px;
  font-family: var(--font-mono, monospace);
  padding: 8px;
  background: rgba(239, 68, 68, 0.08);
  border-radius: 6px;
}

.discovered-tools-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 10px;
  max-height: 200px;
  overflow-y: auto;
}

.tool-item-preview {
  padding: 8px 12px;
  background: var(--el-bg-color);
  border-radius: 6px;
  border: 1px solid var(--el-border-color-lighter);
}

.tool-name {
  font-family: var(--font-mono, monospace);
  font-weight: 600;
  font-size: 13px;
  color: var(--el-color-primary);
}

.tool-desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: 2px;
}

.btn-icon {
  margin-right: 6px;
}
</style>
