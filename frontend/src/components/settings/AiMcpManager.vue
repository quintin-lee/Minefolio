<template>
  <div class="panel-container mcp-panel">
    <div class="panel-header">
      <h3>{{ t('settings.mcpTitle') }}</h3>
    </div>
    <p class="export-hint">{{ t('settings.mcpDesc') }}</p>

    <!-- Stdio 运行机制与指引横幅 -->
    <el-alert
      type="info"
      show-icon
      :closable="false"
      class="mcp-guide-banner"
    >
      <template #title>
        <span class="guide-title">{{ t('settings.mcpGuideTitle') }}</span>
      </template>
      <div class="guide-content">
        {{ t('settings.mcpGuideDesc') }}
      </div>
    </el-alert>

    <div v-loading="loading" class="server-list">
      <div
        v-for="srv in servers"
        :key="srv.id"
        class="server-item"
      >
        <div class="server-header">
          <div class="server-title-wrap">
            <span class="server-name">{{ srv.name }}</span>
            <el-tag size="small" effect="plain">{{ srv.transport === 'http' ? t('settings.mcpTransportHttp') : t('settings.mcpTransportStdio') }}</el-tag>
            <el-tag
              size="small"
              :type="riskTagType(riskType(srv))"
              v-if="riskType(srv)"
            >
              {{ t('settings.mcpRiskLevel') }}: {{ riskType(srv) }}
            </el-tag>
            <el-tag size="small" :type="srv.enabled === 1 ? 'success' : 'info'">
              {{ srv.enabled === 1 ? t('settings.mcpEnabled') : t('settings.mcpDisabled') }}
            </el-tag>
            <el-tag size="small" type="warning" v-if="srv.tool_count != null">
              {{ t('settings.mcpToolCount', { n: srv.tool_count }) }}
            </el-tag>
          </div>
          <div class="server-actions">
            <el-button
              text
              size="small"
              :loading="testingId === srv.id"
              @click="testServer(srv)"
              :aria-label="t('settings.mcpTest')"
              :title="t('settings.mcpTest')"
            >
              <el-icon><Connection /></el-icon>
            </el-button>
            <el-button
              text
              size="small"
              :loading="refreshingId === srv.id"
              @click="refreshServerTools(srv)"
              :aria-label="t('settings.mcpRefresh')"
              :title="t('settings.mcpRefresh')"
            >
              <el-icon><Refresh /></el-icon>
            </el-button>
            <el-button text size="small" @click="toggleTools(srv)" :aria-label="t('settings.mcpTools')" :title="t('settings.mcpTools')">
              <el-icon><Tools /></el-icon>
            </el-button>
            <el-button text size="small" @click="openEdit(srv)" :aria-label="t('common.edit')" :title="t('common.edit')">
              <el-icon><Edit /></el-icon>
            </el-button>
            <el-button text size="small" class="delete-btn" @click="removeServer(srv)" :aria-label="t('common.delete')" :title="t('common.delete')">
              <el-icon><Delete /></el-icon>
            </el-button>
          </div>
        </div>

        <div v-if="expandedServerId === srv.id" class="server-tools">
          <div class="tools-header-bar">
            <span class="tools-bar-title">{{ t('settings.mcpTools') }}</span>
            <el-button
              size="small"
              plain
              :loading="refreshingId === srv.id"
              @click="refreshServerTools(srv)"
            >
              <el-icon><Refresh /></el-icon>
              {{ t('settings.mcpRefresh') }}
            </el-button>
          </div>
          <el-tabs v-if="srvTools[srv.id]" v-model="activeToolTab[srv.id]">
            <el-tab-pane :label="t('settings.mcpTools')" name="tools">
              <div class="tools-list">
                <div v-for="tool in srvTools[srv.id]" :key="tool.id" class="tool-row">
                  <div class="tool-main">
                    <span class="tool-name">{{ tool.qualified_name }}</span>
                    <el-tag size="small" :type="tool.is_mutation === 1 ? 'warning' : 'success'" effect="plain">
                      {{ tool.is_mutation === 1 ? t('settings.mcpMutation') : t('settings.mcpReadOnly') }}
                    </el-tag>
                    <el-tag size="small" :type="riskTagType(tool.risk_level)">{{ tool.risk_level }}</el-tag>
                  </div>
                  <div class="tool-desc" v-if="tool.description">{{ tool.description }}</div>
                </div>
                <div v-if="!srvTools[srv.id]?.length" class="tools-empty">
                  <p>{{ t('settings.mcpToolsEmpty') }}</p>
                  <el-button
                    size="small"
                    type="primary"
                    plain
                    :loading="refreshingId === srv.id"
                    @click="refreshServerTools(srv)"
                  >
                    <el-icon><Refresh /></el-icon>
                    {{ t('settings.mcpRefresh') }}
                  </el-button>
                </div>
              </div>
            </el-tab-pane>
          </el-tabs>
          <div v-else class="tools-empty">
            <p>{{ t('settings.mcpToolsEmpty') }}</p>
            <el-button
              size="small"
              type="primary"
              plain
              :loading="refreshingId === srv.id"
              @click="refreshServerTools(srv)"
            >
              <el-icon><Refresh /></el-icon>
              {{ t('settings.mcpRefresh') }}
            </el-button>
          </div>
        </div>

        <div v-if="editingServerId === srv.id" class="server-form-wrap">
          <el-form :model="editingForm" label-width="120px" class="server-form">
            <el-form-item :label="t('settings.mcpPresets')">
              <el-select
                v-model="selectedPreset"
                :placeholder="t('settings.mcpSelectPreset')"
                @change="applyPreset"
                clearable
                style="width: 100%"
              >
                <el-option
                  v-for="p in presetTemplates"
                  :key="p.value"
                  :label="p.label"
                  :value="p.value"
                />
              </el-select>
            </el-form-item>
            <el-form-item :label="t('settings.mcpName')" required>
              <el-input v-model="editingForm.name" :placeholder="t('settings.mcpNamePlaceholder')" />
            </el-form-item>
            <el-form-item :label="t('settings.mcpTransport')">
              <el-radio-group v-model="editingForm.transport">
                <el-radio value="http">{{ t('settings.mcpTransportHttp') }}</el-radio>
                <el-radio value="stdio">{{ t('settings.mcpTransportStdio') }}</el-radio>
              </el-radio-group>
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpUrl')" required>
              <el-input v-model="editingForm.url" :placeholder="t('settings.mcpUrlPlaceholder')" />
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpHeaders')">
              <div class="json-field-container">
                <el-input v-model="editingForm.headers" type="textarea" :rows="2" :placeholder="t('settings.mcpHeadersPlaceholder')" />
                <el-button v-if="editingForm.headers" size="small" text type="primary" class="format-btn" @click="formatJsonField('headers')">{{ t('settings.mcpFormatJson') }}</el-button>
              </div>
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpCommand')" required>
              <div class="command-box">
                <el-input v-model="editingForm.command" :placeholder="t('settings.mcpCommandPlaceholder')" />
                <div class="command-btn-row">
                  <el-button
                    size="small"
                    type="primary"
                    plain
                    :loading="uploadingScript"
                    @click="triggerUploadScript"
                  >
                    <el-icon><Upload /></el-icon>
                    {{ t('settings.mcpUploadScript') }}
                  </el-button>
                  <el-button
                    size="small"
                    plain
                    @click="openScriptsDrawer"
                  >
                    <el-icon><FolderOpened /></el-icon>
                    {{ t('settings.mcpUploadedScripts') }}
                    <span v-if="uploadedScripts.length" class="script-badge">({{ uploadedScripts.length }})</span>
                  </el-button>
                </div>
              </div>
              <div class="field-tip">{{ t('settings.mcpUploadTypeTip') }}</div>
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpArgs')">
              <div class="json-field-container">
                <el-input v-model="editingForm.args" type="textarea" :rows="2" :placeholder="t('settings.mcpArgsPlaceholder')" />
                <el-button v-if="editingForm.args" size="small" text type="primary" class="format-btn" @click="formatJsonField('args')">{{ t('settings.mcpFormatJson') }}</el-button>
              </div>
            </el-form-item>
            <el-form-item :label="t('settings.mcpEnv')">
              <div class="json-field-container">
                <el-input v-model="editingForm.env" type="textarea" :rows="2" :placeholder="t('settings.mcpEnvPlaceholder')" />
                <el-button v-if="editingForm.env" size="small" text type="primary" class="format-btn" @click="formatJsonField('env')">{{ t('settings.mcpFormatJson') }}</el-button>
              </div>
            </el-form-item>
            <el-form-item :label="t('settings.mcpSecretRef')">
              <el-input v-model="editingForm.secret_ref" :placeholder="t('settings.mcpSecretRefPlaceholder')" />
            </el-form-item>
            <el-form-item :label="t('settings.mcpTimeout')">
              <el-input-number v-model="editingForm.timeout_ms" :min="1000" :max="120000" :step="1000" />
            </el-form-item>
            <el-form-item :label="t('settings.mcpEnabled')">
              <el-switch v-model="editingForm.enabledBool" :active-value="1" :inactive-value="0" />
            </el-form-item>
            <el-form-item>
              <div class="form-actions-bar">
                <el-button size="small" @click="cancelEdit">{{ t('common.cancel') }}</el-button>
                <el-button
                  size="small"
                  type="primary"
                  :loading="saving"
                  @click="saveServer"
                >{{ t('settings.mcpSave') }}</el-button>
              </div>
            </el-form-item>
          </el-form>
        </div>
      </div>

      <!-- 新增 MCP 服务表单卡片 -->
      <div v-if="editingServerId === -1" class="server-item server-form-wrap">
        <div class="server-header">
          <div class="server-title-wrap">
            <span class="server-name">{{ editingForm.name || t('settings.mcpAddServer') }}</span>
            <el-tag size="small" effect="plain">{{ editingForm.transport === 'http' ? t('settings.mcpTransportHttp') : t('settings.mcpTransportStdio') }}</el-tag>
          </div>
        </div>
        <el-form :model="editingForm" label-width="120px" class="server-form">
          <el-form-item :label="t('settings.mcpPresets')">
            <el-select
              v-model="selectedPreset"
              :placeholder="t('settings.mcpSelectPreset')"
              @change="applyPreset"
              clearable
              style="width: 100%"
            >
              <el-option
                v-for="p in presetTemplates"
                :key="p.value"
                :label="p.label"
                :value="p.value"
              />
            </el-select>
          </el-form-item>
          <el-form-item :label="t('settings.mcpName')" required>
            <el-input v-model="editingForm.name" :placeholder="t('settings.mcpNamePlaceholder')" />
          </el-form-item>
          <el-form-item :label="t('settings.mcpTransport')">
            <el-radio-group v-model="editingForm.transport">
              <el-radio value="http">{{ t('settings.mcpTransportHttp') }}</el-radio>
              <el-radio value="stdio">{{ t('settings.mcpTransportStdio') }}</el-radio>
            </el-radio-group>
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpUrl')" required>
            <el-input v-model="editingForm.url" :placeholder="t('settings.mcpUrlPlaceholder')" />
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpHeaders')">
            <div class="json-field-container">
              <el-input v-model="editingForm.headers" type="textarea" :rows="2" :placeholder="t('settings.mcpHeadersPlaceholder')" />
              <el-button v-if="editingForm.headers" size="small" text type="primary" class="format-btn" @click="formatJsonField('headers')">{{ t('settings.mcpFormatJson') }}</el-button>
            </div>
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpCommand')" required>
            <div class="command-box">
              <el-input v-model="editingForm.command" :placeholder="t('settings.mcpCommandPlaceholder')" />
              <div class="command-btn-row">
                <el-button
                  size="small"
                  type="primary"
                  plain
                  :loading="uploadingScript"
                  @click="triggerUploadScript"
                >
                  <el-icon><Upload /></el-icon>
                  {{ t('settings.mcpUploadScript') }}
                </el-button>
                <el-button
                  size="small"
                  plain
                  @click="openScriptsDrawer"
                >
                  <el-icon><FolderOpened /></el-icon>
                  {{ t('settings.mcpUploadedScripts') }}
                  <span v-if="uploadedScripts.length" class="script-badge">({{ uploadedScripts.length }})</span>
                </el-button>
              </div>
            </div>
            <div class="field-tip">{{ t('settings.mcpUploadTypeTip') }}</div>
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpArgs')">
            <div class="json-field-container">
              <el-input v-model="editingForm.args" type="textarea" :rows="2" :placeholder="t('settings.mcpArgsPlaceholder')" />
              <el-button v-if="editingForm.args" size="small" text type="primary" class="format-btn" @click="formatJsonField('args')">{{ t('settings.mcpFormatJson') }}</el-button>
            </div>
          </el-form-item>
          <el-form-item :label="t('settings.mcpEnv')">
            <div class="json-field-container">
              <el-input v-model="editingForm.env" type="textarea" :rows="2" :placeholder="t('settings.mcpEnvPlaceholder')" />
              <el-button v-if="editingForm.env" size="small" text type="primary" class="format-btn" @click="formatJsonField('env')">{{ t('settings.mcpFormatJson') }}</el-button>
            </div>
          </el-form-item>
          <el-form-item :label="t('settings.mcpSecretRef')">
            <el-input v-model="editingForm.secret_ref" :placeholder="t('settings.mcpSecretRefPlaceholder')" />
          </el-form-item>
          <el-form-item :label="t('settings.mcpTimeout')">
            <el-input-number v-model="editingForm.timeout_ms" :min="1000" :max="120000" :step="1000" />
          </el-form-item>
          <el-form-item :label="t('settings.mcpEnabled')">
            <el-switch v-model="editingForm.enabledBool" :active-value="1" :inactive-value="0" />
          </el-form-item>
          <el-form-item>
            <div class="form-actions-bar">
              <el-button size="small" @click="cancelEdit">{{ t('common.cancel') }}</el-button>
              <el-button
                size="small"
                type="primary"
                :loading="saving"
                @click="saveServer"
              >{{ t('settings.mcpSave') }}</el-button>
            </div>
          </el-form-item>
        </el-form>
      </div>

      <el-button
        v-if="editingServerId !== -1"
        type="primary"
        plain
        @click="addServer"
        class="add-server-btn"
        :aria-label="t('settings.mcpAddServer')"
      >
        <el-icon><Plus /></el-icon>
        {{ t('settings.mcpAddServer') }}
      </el-button>

      <el-empty v-if="!loading && !servers.length && editingServerId !== -1" :description="t('settings.mcpEmpty')" />
    </div>

    <!-- 隐藏的自定义脚本文件选择器 -->
    <input
      ref="fileInputRef"
      type="file"
      accept=".py,.js,.mjs,.sh"
      style="display: none"
      @change="onFileSelected"
    />

    <!-- 已上传脚本管理抽屉 -->
    <el-drawer
      v-model="scriptsDrawerVisible"
      :title="t('settings.mcpUploadedScripts')"
      size="440px"
      :destroy-on-close="true"
    >
      <div v-loading="loadingScripts" class="scripts-drawer-content">
        <div class="scripts-drawer-header">
          <el-button
            size="small"
            type="primary"
            plain
            :loading="uploadingScript"
            @click="triggerUploadScript"
          >
            <el-icon><Upload /></el-icon>
            {{ t('settings.mcpUploadScript') }}
          </el-button>
          <el-button size="small" text @click="loadUploadedScripts">
            <el-icon><Refresh /></el-icon>
            {{ t('settings.mcpRefresh') }}
          </el-button>
        </div>
        <div v-if="uploadedScripts.length" class="scripts-list">
          <div v-for="item in uploadedScripts" :key="item.filename" class="script-card">
            <div class="script-card-header">
              <span class="script-name">{{ item.filename }}</span>
              <span class="script-size">{{ formatFileSize(item.size) }}</span>
            </div>
            <div class="script-cmd-box">
              <code>{{ item.command }}</code>
            </div>
            <div class="script-card-footer">
              <el-button size="small" type="primary" link @click="useUploadedScript(item)">
                {{ t('settings.mcpUseScript') }}
              </el-button>
              <el-button size="small" type="danger" link @click="removeUploadedScript(item)">
                {{ t('settings.mcpDeleteScript') }}
              </el-button>
            </div>
          </div>
        </div>
        <el-empty v-else :description="t('settings.mcpNoUploadedScripts')" />
      </div>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Edit, Delete, Plus, Tools, Connection, Refresh, Upload, FolderOpened } from '@element-plus/icons-vue'
import {
  listMcpServers,
  createMcpServer,
  updateMcpServer,
  deleteMcpServer,
  getMcpServerTools,
  testMcpServer,
  refreshMcpServer,
  uploadMcpScript,
  listMcpScripts,
  deleteMcpScript,
} from '@/api/ai-mcp'
import type { McpServer, McpServerTool, McpServerInput, McpScriptItem } from '@/types'
import { t } from '@/utils/locale'

interface EditableServer extends McpServerInput {
  enabledBool: number
}

const servers = ref<McpServer[]>([])
const loading = ref(false)
const saving = ref(false)
const testingId = ref<number | null>(null)
const refreshingId = ref<number | null>(null)
const editingServerId = ref<number | null>(null)
const expandedServerId = ref<number | null>(null)
const srvTools = ref<Record<number, McpServerTool[]>>({})
const activeToolTab = ref<Record<number, string>>({})

const selectedPreset = ref('')
const fileInputRef = ref<HTMLInputElement | null>(null)
const uploadingScript = ref(false)
const scriptsDrawerVisible = ref(false)
const loadingScripts = ref(false)
const uploadedScripts = ref<McpScriptItem[]>([])

const presetTemplates = computed(() => [
  {
    value: 'npx-fs',
    label: t('settings.mcpPresetNpxFs'),
    transport: 'stdio' as const,
    name: 'filesystem',
    command: 'npx -y @modelcontextprotocol/server-filesystem /tmp',
    args: '',
    env: '',
  },
  {
    value: 'uvx-fetch',
    label: t('settings.mcpPresetUvxFetch'),
    transport: 'stdio' as const,
    name: 'fetch',
    command: 'uvx mcp-server-fetch',
    args: '',
    env: '',
  },
  {
    value: 'uvx-sqlite',
    label: t('settings.mcpPresetUvxSqlite'),
    transport: 'stdio' as const,
    name: 'sqlite',
    command: 'uvx mcp-server-sqlite --db-path ./data/minefolio.db',
    args: '',
    env: '',
  },
  {
    value: 'npx-memory',
    label: t('settings.mcpPresetNpxMemory'),
    transport: 'stdio' as const,
    name: 'memory',
    command: 'npx -y @modelcontextprotocol/server-memory',
    args: '',
    env: '',
  },
  {
    value: 'custom-py',
    label: t('settings.mcpPresetCustomPy'),
    transport: 'stdio' as const,
    name: 'my-python-tool',
    command: 'python3 /path/to/script.py',
    args: '[]',
    env: '{\n  "PYTHONUNBUFFERED": "1"\n}',
  },
  {
    value: 'custom-node',
    label: t('settings.mcpPresetCustomNode'),
    transport: 'stdio' as const,
    name: 'my-node-tool',
    command: 'node /path/to/script.js',
    args: '[]',
    env: '{\n  "NODE_ENV": "production"\n}',
  },
])

function applyPreset(val: string) {
  if (!val) return
  const tmpl = presetTemplates.value.find((p) => p.value === val)
  if (!tmpl) return
  editingForm.transport = tmpl.transport
  editingForm.name = tmpl.name
  editingForm.command = tmpl.command
  editingForm.args = tmpl.args
  editingForm.env = tmpl.env
}

const editingForm = reactive<EditableServer>({
  name: '',
  transport: 'http',
  url: '',
  command: '',
  args: '',
  env: '',
  headers: '',
  secret_ref: '',
  enabledBool: 1,
  timeout_ms: 30000,
})

async function loadServers() {
  loading.value = true
  try {
    const list = await listMcpServers()
    servers.value = list || []
  } catch {
    servers.value = []
  } finally {
    loading.value = false
  }
}

function riskTagType(risk: string | undefined) {
  if (risk === 'high') return 'danger'
  if (risk === 'medium') return 'warning'
  return 'success'
}

function riskType(srv: McpServer) {
  const tools = srvTools.value[srv.id]
  if (tools && tools.length > 0) {
    if (tools.some((x) => x.risk_level === 'high')) return 'high'
    if (tools.some((x) => x.risk_level === 'medium')) return 'medium'
    return 'low'
  }
  return undefined
}

function toggleTools(srv: McpServer) {
  if (expandedServerId.value === srv.id) {
    expandedServerId.value = null
    return
  }
  expandedServerId.value = srv.id
  if (activeToolTab.value[srv.id] == null) activeToolTab.value[srv.id] = 'tools'
  if (!srvTools.value[srv.id]) {
    getMcpServerTools(srv.id)
      .then((tools) => {
        srvTools.value[srv.id] = tools || []
      })
      .catch(() => {
        srvTools.value[srv.id] = []
      })
  }
}

function openEdit(srv: McpServer) {
  if (editingServerId.value === srv.id) {
    editingServerId.value = null
    return
  }
  editingServerId.value = srv.id
  selectedPreset.value = ''
  Object.assign(editingForm, {
    name: srv.name,
    transport: srv.transport,
    url: srv.url || '',
    command: srv.command || '',
    args: srv.args || '',
    env: srv.env || '',
    headers: srv.headers || '',
    secret_ref: srv.secret_ref || '',
    enabledBool: srv.enabled ? 1 : 0,
    timeout_ms: srv.timeout_ms || 30000,
  })
  expandedServerId.value = null
}

function cancelEdit() {
  editingServerId.value = null
  selectedPreset.value = ''
}

function addServer() {
  selectedPreset.value = ''
  Object.assign(editingForm, {
    name: '',
    transport: 'http',
    url: '',
    command: '',
    args: '',
    env: '',
    headers: '',
    secret_ref: '',
    enabledBool: 1,
    timeout_ms: 30000,
  })
  expandedServerId.value = null
  editingServerId.value = -1
}

function formatJsonField(field: 'args' | 'env' | 'headers') {
  const val = editingForm[field]
  if (!val || !val.trim()) return
  try {
    const parsed = JSON.parse(val)
    editingForm[field] = JSON.stringify(parsed, null, 2)
  } catch {
    ElMessage.warning(t('settings.mcpInvalidJson'))
  }
}

function triggerUploadScript() {
  fileInputRef.value?.click()
}

async function onFileSelected(e: Event) {
  const target = e.target as HTMLInputElement
  const file = target.files?.[0]
  if (!file) return
  if (file.size > 5 * 1024 * 1024) {
    ElMessage.error(t('settings.mcpUploadTypeTip'))
    target.value = ''
    return
  }
  uploadingScript.value = true
  try {
    const res = await uploadMcpScript(file)
    editingForm.command = res.command
    if (!editingForm.name) {
      editingForm.name = res.filename
        .replace(/\.[^.]+$/, '')
        .toLowerCase()
        .replace(/[^a-z0-9_-]/g, '_')
        .slice(0, 64)
    }
    ElMessage.success(t('settings.mcpUploadSuccess'))
    loadUploadedScripts()
  } catch (err: unknown) {
    const msg =
      (err as { response?: { data?: { message?: string } } })?.response?.data?.message ||
      (err as { message?: string })?.message ||
      t('settings.mcpUploadFailed')
    ElMessage.error(msg)
  } finally {
    uploadingScript.value = false
    target.value = ''
  }
}

async function loadUploadedScripts() {
  loadingScripts.value = true
  try {
    const list = await listMcpScripts()
    uploadedScripts.value = list || []
  } catch {
    uploadedScripts.value = []
  } finally {
    loadingScripts.value = false
  }
}

function openScriptsDrawer() {
  scriptsDrawerVisible.value = true
  loadUploadedScripts()
}

function useUploadedScript(item: McpScriptItem) {
  editingForm.command = item.command
  if (!editingForm.name) {
    editingForm.name = item.filename
      .replace(/\.[^.]+$/, '')
      .toLowerCase()
      .replace(/[^a-z0-9_-]/g, '_')
      .slice(0, 64)
  }
  scriptsDrawerVisible.value = false
  ElMessage.success(t('settings.mcpUploadSuccess'))
}

async function removeUploadedScript(item: McpScriptItem) {
  try {
    await ElMessageBox.confirm(
      t('settings.mcpConfirmDeleteScript', { name: item.filename }),
      t('common.hint'),
      { type: 'warning' }
    )
  } catch {
    return
  }
  try {
    await deleteMcpScript(item.filename)
    ElMessage.success(t('settings.mcpDeleteSuccess'))
    await loadUploadedScripts()
  } catch {
    ElMessage.error(t('settings.mcpSaveFailed'))
  }
}

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / (1024 * 1024)).toFixed(1) + ' MB'
}

function buildInput(): McpServerInput {
  const input: McpServerInput = {
    name: editingForm.name.trim(),
    transport: editingForm.transport,
    enabled: Number(editingForm.enabledBool) ? 1 : 0,
    timeout_ms: editingForm.timeout_ms || 30000,
  }
  if (editingForm.transport === 'http') {
    if (editingForm.url) input.url = editingForm.url.trim()
    if (editingForm.headers) input.headers = editingForm.headers.trim()
    if (editingForm.env) input.env = editingForm.env.trim()
    if (editingForm.secret_ref) input.secret_ref = editingForm.secret_ref.trim()
  } else {
    if (editingForm.command) input.command = editingForm.command.trim()
    if (editingForm.args) input.args = editingForm.args.trim()
    if (editingForm.env) input.env = editingForm.env.trim()
    if (editingForm.secret_ref) input.secret_ref = editingForm.secret_ref.trim()
  }
  return input
}

async function saveServer() {
  const trimmedName = editingForm.name.trim()
  if (!trimmedName) {
    ElMessage.error(t('settings.mcpNameRequired'))
    return
  }
  if (!/^[a-z0-9_-]{1,64}$/.test(trimmedName)) {
    ElMessage.error(t('settings.mcpNamePlaceholder'))
    return
  }
  if (editingForm.transport === 'http' && !editingForm.url?.trim()) {
    ElMessage.error(t('settings.mcpUrlPlaceholder'))
    return
  }
  if (editingForm.transport === 'stdio' && !editingForm.command?.trim()) {
    ElMessage.error(t('settings.mcpCommandPlaceholder'))
    return
  }

  /* JSON 语法校验 */
  if (editingForm.args?.trim()) {
    try {
      const parsed = JSON.parse(editingForm.args)
      if (!Array.isArray(parsed)) {
        ElMessage.error(t('settings.mcpArgs') + ': ' + t('settings.mcpInvalidJson') + ' (Array expected)')
        return
      }
    } catch {
      ElMessage.error(t('settings.mcpArgs') + ': ' + t('settings.mcpInvalidJson'))
      return
    }
  }
  if (editingForm.env?.trim()) {
    try {
      const parsed = JSON.parse(editingForm.env)
      if (typeof parsed !== 'object' || Array.isArray(parsed) || parsed === null) {
        ElMessage.error(t('settings.mcpEnv') + ': ' + t('settings.mcpInvalidJson') + ' (Object expected)')
        return
      }
    } catch {
      ElMessage.error(t('settings.mcpEnv') + ': ' + t('settings.mcpInvalidJson'))
      return
    }
  }
  if (editingForm.headers?.trim()) {
    try {
      const parsed = JSON.parse(editingForm.headers)
      if (typeof parsed !== 'object' || Array.isArray(parsed) || parsed === null) {
        ElMessage.error(t('settings.mcpHeaders') + ': ' + t('settings.mcpInvalidJson') + ' (Object expected)')
        return
      }
    } catch {
      ElMessage.error(t('settings.mcpHeaders') + ': ' + t('settings.mcpInvalidJson'))
      return
    }
  }

  saving.value = true
  try {
    if (editingServerId.value === -1) {
      const created = await createMcpServer(buildInput())
      ElMessage.success(t('settings.mcpServerAdded'))
      editingServerId.value = null
      await loadServers()
      if (created && created.id > 0) {
        refreshMcpServer(created.id)
          .then((res) => {
            if (res.tool_count > 0) {
              ElMessage.success(t('settings.mcpRefreshSuccess', { n: res.tool_count }))
              loadServers()
            }
          })
          .catch(() => {})
      }
    } else if (editingServerId.value != null) {
      await updateMcpServer(editingServerId.value, buildInput())
      ElMessage.success(t('settings.mcpServerSaved'))
      editingServerId.value = null
      await loadServers()
    }
  } catch (err: unknown) {
    const msg =
      (err as { response?: { data?: { message?: string } } })?.response?.data?.message ||
      (err as { message?: string })?.message ||
      t('settings.mcpSaveFailed')
    ElMessage.error(msg)
  } finally {
    saving.value = false
  }
}

async function testServer(srv: McpServer) {
  testingId.value = srv.id
  try {
    const res = await testMcpServer(srv.id)
    if (res.status === 'ok') {
      ElMessage.success(t('settings.mcpTestSuccess', { n: res.tool_count ?? 0 }))
      await loadServers()
      if (expandedServerId.value === srv.id) {
        const tools = await getMcpServerTools(srv.id)
        srvTools.value[srv.id] = tools || []
      }
    } else {
      ElMessage.error(res.message || t('settings.mcpTestFailed'))
    }
  } catch (err: unknown) {
    const msg =
      (err as { response?: { data?: { message?: string } } })?.response?.data?.message ||
      (err as { message?: string })?.message ||
      t('settings.mcpTestFailed')
    ElMessage.error(msg)
  } finally {
    testingId.value = null
  }
}

async function refreshServerTools(srv: McpServer) {
  refreshingId.value = srv.id
  try {
    const res = await refreshMcpServer(srv.id)
    ElMessage.success(t('settings.mcpRefreshSuccess', { n: res.tool_count ?? 0 }))
    await loadServers()
    const tools = await getMcpServerTools(srv.id)
    srvTools.value[srv.id] = tools || []
  } catch (err: unknown) {
    const msg =
      (err as { response?: { data?: { message?: string } } })?.response?.data?.message ||
      (err as { message?: string })?.message ||
      t('settings.mcpRefreshFailed')
    ElMessage.error(msg)
  } finally {
    refreshingId.value = null
  }
}

async function removeServer(srv: McpServer) {
  try {
    await ElMessageBox.confirm(t('settings.mcpConfirmDelete', { name: srv.name }), t('common.hint'), {
      type: 'warning',
    })
  } catch {
    return
  }
  try {
    await deleteMcpServer(srv.id)
    delete srvTools.value[srv.id]
    delete activeToolTab.value[srv.id]
    ElMessage.success(t('settings.mcpServerDeleted'))
    await loadServers()
  } catch {
    ElMessage.error(t('settings.mcpSaveFailed'))
  }
}

loadServers()
loadUploadedScripts()
</script>

<style scoped>
.mcp-panel {
  margin-bottom: 8px;
}

.tools-header-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
  padding-bottom: 4px;
  border-bottom: 1px dashed var(--mf-border);
}

.tools-bar-title {
  font-weight: 500;
  font-size: 13px;
  color: var(--mf-text-main);
}

.server-list {
  min-height: 40px;
}

.server-item {
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 12px;
  margin-bottom: 12px;
  background: var(--mf-surface);
}

.server-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.server-name {
  font-weight: 500;
  color: var(--mf-text-main);
  font-size: 14px;
}

.server-title-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.server-actions {
  display: flex;
  gap: 4px;
}

.delete-btn:hover {
  color: var(--mf-danger);
}

.server-form-wrap {
  margin-top: 12px;
}

.server-form {
  margin-top: 8px;
}

.form-actions-bar {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  width: 100%;
}

.server-tools {
  margin-top: 8px;
  padding: 8px;
  background: var(--mf-surface-muted);
  border-radius: 6px;
}

.tools-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.tool-row {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.tool-main {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.tool-name {
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--mf-text-main);
}

.tool-desc {
  font-size: 12px;
  color: var(--mf-text-muted);
}

.tools-empty {
  font-size: 12px;
  color: var(--mf-text-muted);
}

.add-server-btn {
  width: 100%;
  margin-top: 8px;
}

:deep(.el-divider) {
  margin: 24px 0;
}

.mcp-guide-banner {
  margin-bottom: 16px;
  border-radius: 8px;
}

.guide-title {
  font-weight: 600;
  font-size: 13px;
}

.guide-content {
  font-size: 12px;
  margin-top: 4px;
  line-height: 1.6;
  color: var(--mf-text-muted);
}

.command-box {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: 100%;
}

.command-btn-row {
  display: flex;
  gap: 8px;
  align-items: center;
}

.field-tip {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-top: 4px;
  line-height: 1.4;
}

.json-field-container {
  position: relative;
  width: 100%;
}

.format-btn {
  position: absolute;
  right: 8px;
  top: 6px;
  z-index: 2;
  padding: 2px 6px;
  height: 22px;
  font-size: 11px;
}

.script-badge {
  margin-left: 4px;
  font-size: 11px;
  opacity: 0.85;
}

.scripts-drawer-content {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
}

.scripts-drawer-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--mf-border);
}

.scripts-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
  overflow-y: auto;
}

.script-card {
  padding: 10px;
  border: 1px solid var(--mf-border);
  border-radius: 6px;
  background: var(--mf-surface-muted);
}

.script-card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
}

.script-name {
  font-weight: 600;
  font-size: 13px;
  color: var(--mf-text-main);
  font-family: var(--font-mono, monospace);
}

.script-size {
  font-size: 11px;
  color: var(--mf-text-muted);
}

.script-cmd-box {
  padding: 6px 8px;
  background: var(--mf-surface);
  border-radius: 4px;
  border: 1px solid var(--mf-border);
  font-size: 11px;
  margin-bottom: 6px;
  word-break: break-all;
  font-family: var(--font-mono, monospace);
  color: var(--mf-text-main);
}

.script-card-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
