<template>
  <div class="panel-container mcp-panel">
    <div class="panel-header">
      <h3>{{ t('settings.mcpTitle') }}</h3>
    </div>
    <p class="export-hint">{{ t('settings.mcpDesc') }}</p>

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
          <el-form :model="editingForm" label-width="110px" class="server-form">
            <el-form-item :label="t('settings.mcpName')">
              <el-input v-model="editingForm.name" :placeholder="t('settings.mcpNamePlaceholder')" />
            </el-form-item>
            <el-form-item :label="t('settings.mcpTransport')">
              <el-radio-group v-model="editingForm.transport">
                <el-radio value="http">{{ t('settings.mcpTransportHttp') }}</el-radio>
                <el-radio value="stdio">{{ t('settings.mcpTransportStdio') }}</el-radio>
              </el-radio-group>
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpUrl')">
              <el-input v-model="editingForm.url" :placeholder="t('settings.mcpUrlPlaceholder')" />
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'http'" :label="t('settings.mcpHeaders')">
              <el-input v-model="editingForm.headers" type="textarea" :rows="2" :placeholder="t('settings.mcpHeadersPlaceholder')" />
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpCommand')">
              <el-input v-model="editingForm.command" :placeholder="t('settings.mcpCommandPlaceholder')" />
            </el-form-item>
            <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpArgs')">
              <el-input v-model="editingForm.args" type="textarea" :rows="2" :placeholder="t('settings.mcpArgsPlaceholder')" />
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
        <el-form :model="editingForm" label-width="110px" class="server-form">
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
            <el-input v-model="editingForm.headers" type="textarea" :rows="2" :placeholder="t('settings.mcpHeadersPlaceholder')" />
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpCommand')" required>
            <el-input v-model="editingForm.command" :placeholder="t('settings.mcpCommandPlaceholder')" />
          </el-form-item>
          <el-form-item v-if="editingForm.transport === 'stdio'" :label="t('settings.mcpArgs')">
            <el-input v-model="editingForm.args" type="textarea" :rows="2" :placeholder="t('settings.mcpArgsPlaceholder')" />
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
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Edit, Delete, Plus, Tools, Connection, Refresh } from '@element-plus/icons-vue'
import {
  listMcpServers,
  createMcpServer,
  updateMcpServer,
  deleteMcpServer,
  getMcpServerTools,
  testMcpServer,
  refreshMcpServer,
} from '@/api/ai-mcp'
import type { McpServer, McpServerTool, McpServerInput } from '@/types'
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

const editingForm = reactive<EditableServer>({
  name: '',
  transport: 'http',
  url: '',
  command: '',
  args: '',
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
  Object.assign(editingForm, {
    name: srv.name,
    transport: srv.transport,
    url: srv.url || '',
    command: srv.command || '',
    args: srv.args || '',
    headers: srv.headers || '',
    secret_ref: srv.secret_ref || '',
    enabledBool: srv.enabled ? 1 : 0,
    timeout_ms: srv.timeout_ms || 30000,
  })
  expandedServerId.value = null
}

function cancelEdit() {
  editingServerId.value = null
}

function addServer() {
  Object.assign(editingForm, {
    name: '',
    transport: 'http',
    url: '',
    command: '',
    args: '',
    headers: '',
    secret_ref: '',
    enabledBool: 1,
    timeout_ms: 30000,
  })
  expandedServerId.value = null
  editingServerId.value = -1
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
    if (editingForm.secret_ref) input.secret_ref = editingForm.secret_ref.trim()
  } else {
    if (editingForm.command) input.command = editingForm.command.trim()
    if (editingForm.args) input.args = editingForm.args.trim()
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
</style>
