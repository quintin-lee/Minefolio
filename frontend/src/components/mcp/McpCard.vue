<template>
  <el-card class="mcp-card" :class="{ 'is-disabled': Number(server.enabled) === 0 }" shadow="hover">
    <div class="card-header">
      <div class="header-left">
        <div class="transport-icon-box" :class="`transport-${server.transport}`">
          <Icon :icon="server.transport === 'http' ? 'ph:globe-duotone' : 'ph:terminal-window-duotone'" class="server-icon" />
        </div>
        <div class="title-meta">
          <div class="server-name" :title="server.name">{{ server.name }}</div>
          <div class="badge-row">
            <el-tag size="small" :type="server.transport === 'http' ? 'primary' : 'warning'" effect="light">
              {{ server.transport === 'http' ? 'HTTP/SSE 远端' : 'Stdio 本地进程' }}
            </el-tag>
            <el-tag v-if="server.tool_count != null" size="small" type="success" effect="plain">
              <Icon icon="ph:wrench-duotone" class="mini-tag-icon" />
              {{ server.tool_count }} 个工具
            </el-tag>
            <el-tag v-if="server.discovered_at" size="small" type="info" effect="plain" class="time-tag">
              已探活
            </el-tag>
            <el-tag v-else size="small" type="danger" effect="plain" class="time-tag">
              未探活
            </el-tag>
          </div>
        </div>
      </div>
      <div class="header-right" @click.stop>
        <el-switch
          :model-value="Number(server.enabled) === 1"
          active-text=""
          inactive-text=""
          :loading="toggling"
          :before-change="handleBeforeChange"
        />
      </div>
    </div>

    <div class="card-body">
      <!-- Target command or URL box -->
      <div class="target-box" :title="server.transport === 'http' ? server.url : server.command">
        <Icon :icon="server.transport === 'http' ? 'ph:link-bold' : 'ph:terminal-bold'" class="box-icon" />
        <span class="box-text">{{ (server.transport === 'http' ? server.url : server.command) || '未配置端点' }}</span>
      </div>

      <!-- Discovered Tools tags preview -->
      <div v-if="tools && tools.length > 0" class="server-tools-preview">
        <div class="tools-label">可用工具:</div>
        <div class="tools-chips">
          <el-tag
            v-for="t in tools.slice(0, 4)"
            :key="t.tool_name"
            size="small"
            class="tool-chip"
            effect="plain"
            @click="$emit('view-tools', server)"
          >
            #{{ t.tool_name }}
          </el-tag>
          <span v-if="tools.length > 4" class="more-tools" @click="$emit('view-tools', server)">
            +{{ tools.length - 4 }} 更多
          </span>
        </div>
      </div>
      <div v-else class="empty-tools-hint">
        <span>暂未同步工具列表，可点击下方探活按钮拉取</span>
      </div>
    </div>

    <div class="card-footer">
      <div class="meta-info">
        <span class="timeout-hint" :title="`超时设置: ${server.timeout_ms || 30000}ms`">
          <Icon icon="ph:timer" />
          {{ server.timeout_ms || 30000 }}ms
        </span>
        <span v-if="server.secret_ref" class="secret-hint" :title="`凭证引用: ${server.secret_ref}`">
          <Icon icon="ph:key-duotone" />
          {{ server.secret_ref }}
        </span>
      </div>

      <div class="action-buttons">
        <el-tooltip content="探活与工具探测" placement="top" :show-after="400">
          <el-button text circle size="small" :loading="testing" @click="$emit('test', server)">
            <Icon icon="ph:broadcast-duotone" class="action-icon test-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="强制刷新工具列表" placement="top" :show-after="400">
          <el-button text circle size="small" :loading="refreshing" @click="$emit('refresh', server)">
            <Icon icon="ph:arrows-clockwise-duotone" class="action-icon refresh-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="查看工具详情及 Schema" placement="top" :show-after="400">
          <el-button text circle size="small" @click="$emit('view-tools', server)">
            <Icon icon="ph:wrench-duotone" class="action-icon tools-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="编辑服务器配置" placement="top" :show-after="400">
          <el-button text circle size="small" @click="$emit('edit', server)">
            <Icon icon="ph:pencil-simple-duotone" class="action-icon" />
          </el-button>
        </el-tooltip>

        <el-tooltip content="删除 MCP 服务" placement="top" :show-after="400">
          <el-button text circle size="small" class="del-btn" @click="$emit('delete', server)">
            <Icon icon="ph:trash-duotone" class="action-icon del-icon" />
          </el-button>
        </el-tooltip>
      </div>
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { McpServer, McpServerTool } from '@/types'
import { updateMcpServer } from '@/api/ai-mcp'

const props = defineProps<{
  server: McpServer
  tools?: McpServerTool[]
  testing?: boolean
  refreshing?: boolean
}>()

const emit = defineEmits<{
  (e: 'edit', server: McpServer): void
  (e: 'delete', server: McpServer): void
  (e: 'toggle', server: McpServer, enabled: boolean): void
  (e: 'test', server: McpServer): void
  (e: 'refresh', server: McpServer): void
  (e: 'view-tools', server: McpServer): void
}>()

const toggling = ref(false)

async function handleBeforeChange(): Promise<boolean> {
  const currentVal = Number(props.server.enabled) === 1
  const targetVal = !currentVal
  toggling.value = true
  try {
    await updateMcpServer(props.server.id, {
      name: props.server.name,
      transport: props.server.transport,
      url: props.server.url,
      command: props.server.command,
      args: props.server.args,
      env: props.server.env,
      headers: props.server.headers,
      secret_ref: props.server.secret_ref,
      timeout_ms: props.server.timeout_ms,
      enabled: targetVal ? 1 : 0,
    })
    props.server.enabled = targetVal ? 1 : 0
    ElMessage.success(targetVal ? `MCP服务「${props.server.name}」已启用` : `MCP服务「${props.server.name}」已停用`)
    emit('toggle', props.server, targetVal)
    return true
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '切换状态失败')
    return false
  } finally {
    toggling.value = false
  }
}
</script>

<style scoped>
.mcp-card {
  border-radius: 12px;
  border: 1px solid var(--el-border-color-light);
  background: var(--el-bg-color);
  transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  display: flex;
  flex-direction: column;
}

.mcp-card:hover {
  border-color: var(--el-color-primary-light-5);
  transform: translateY(-2px);
  box-shadow: 0 8px 24px -4px rgba(0, 0, 0, 0.08);
}

.mcp-card.is-disabled {
  opacity: 0.68;
  filter: grayscale(0.2);
}

.card-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.header-left {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  min-width: 0;
  flex: 1;
}

.transport-icon-box {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  background: var(--el-fill-color-light);
  color: var(--el-color-primary);
  font-size: 22px;
}

.transport-icon-box.transport-stdio {
  background: rgba(245, 158, 11, 0.12);
  color: #f59e0b;
}

.transport-icon-box.transport-http {
  background: rgba(139, 92, 246, 0.12);
  color: #8b5cf6;
}

.title-meta {
  min-width: 0;
  flex: 1;
}

.server-name {
  font-size: 15px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  font-family: var(--font-mono, monospace);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-bottom: 6px;
}

.badge-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
}

.mini-tag-icon {
  font-size: 12px;
  margin-right: 3px;
  vertical-align: -1px;
}

.card-body {
  flex: 1;
  min-height: 72px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}

.target-box {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  background: var(--el-fill-color-light);
  border-radius: 6px;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--el-text-color-regular);
  margin-bottom: 10px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.box-icon {
  flex-shrink: 0;
  font-size: 14px;
  color: var(--el-text-color-secondary);
}

.box-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.server-tools-preview {
  margin-bottom: 8px;
}

.tools-label {
  font-size: 11px;
  color: var(--el-text-color-secondary);
  margin-bottom: 4px;
}

.tools-chips {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
}

.tool-chip {
  font-size: 11px;
  padding: 0 6px;
  height: 20px;
  line-height: 18px;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}

.tool-chip:hover {
  border-color: var(--el-color-primary);
  color: var(--el-color-primary);
}

.more-tools {
  font-size: 11px;
  color: var(--el-color-primary);
  cursor: pointer;
  margin-left: 2px;
}

.more-tools:hover {
  text-decoration: underline;
}

.empty-tools-hint {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
  line-height: 1.5;
  margin-bottom: 8px;
}

.card-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding-top: 12px;
  margin-top: 8px;
  border-top: 1px solid var(--el-border-color-lighter);
}

.meta-info {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 12px;
  color: var(--el-text-color-placeholder);
}

.meta-info span {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}

.action-buttons {
  display: flex;
  align-items: center;
  gap: 2px;
}

.action-icon {
  font-size: 17px;
  color: var(--el-text-color-regular);
  transition: all 0.2s;
}

.action-buttons :deep(.el-button:hover) .action-icon {
  color: var(--el-color-primary);
  transform: scale(1.1);
}

.test-icon:hover {
  color: #10b981 !important;
}

.refresh-icon:hover {
  color: #3b82f6 !important;
}

.tools-icon:hover {
  color: #8b5cf6 !important;
}

.action-buttons :deep(.del-btn:hover) .del-icon {
  color: var(--el-color-danger) !important;
}
</style>
