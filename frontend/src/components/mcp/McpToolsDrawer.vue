<template>
  <el-drawer
    v-model="visible"
    :title="server ? `MCP 工具清单 — ${server.name}` : 'MCP 工具清单'"
    size="520px"
    destroy-on-close
  >
    <div v-loading="loading" class="tools-drawer-content">
      <div v-if="server" class="drawer-header-meta">
        <div class="meta-tag-row">
          <el-tag :type="server.transport === 'http' ? 'primary' : 'warning'" size="small">
            {{ server.transport.toUpperCase() }}
          </el-tag>
          <el-tag size="small" type="success">
            共发现 {{ tools.length }} 个可用工具
          </el-tag>
          <el-tag size="small" :type="Number(server.enabled) === 1 ? 'success' : 'info'">
            {{ Number(server.enabled) === 1 ? '已启用 (LLM可调度)' : '已停用' }}
          </el-tag>
        </div>
        <div class="command-box">
          <code>{{ (server.transport === 'http' ? server.url : server.command) || '-' }}</code>
        </div>
      </div>

      <div class="action-bar-top">
        <el-input
          v-model="filterQuery"
          placeholder="按工具名称或说明过滤..."
          clearable
          size="small"
          class="filter-input"
        >
          <template #prefix>
            <Icon icon="ph:magnifying-glass" />
          </template>
        </el-input>

        <el-button
          size="small"
          :loading="refreshing"
          @click="handleRefresh"
        >
          <Icon icon="ph:arrows-clockwise" class="btn-icon" />
          刷新工具
        </el-button>
      </div>

      <div v-if="filteredTools.length > 0" class="tools-list">
        <div
          v-for="tool in filteredTools"
          :key="tool.tool_name"
          class="tool-card"
        >
          <div class="tool-card-header">
            <span class="tool-name">
              <Icon icon="ph:wrench-duotone" class="wrench-icon" />
              {{ tool.tool_name }}
            </span>
            <el-button
              text
              size="small"
              class="copy-btn"
              @click="copyText(tool.tool_name, '已复制工具名称')"
            >
              <Icon icon="ph:copy-duotone" />
              复制名
            </el-button>
          </div>

          <p class="tool-desc">{{ tool.description || '暂无工具描述说明' }}</p>

          <!-- Input Schema Collapsible -->
          <div v-if="tool.input_schema" class="schema-collapse-wrap">
            <el-collapse>
              <el-collapse-item title="查看输入参数 Schema (JSON Schema)" name="1">
                <div class="schema-box">
                  <div class="schema-header">
                    <span>入参定义</span>
                    <el-button
                      text
                      size="small"
                      @click="copyText(formatSchema(tool.input_schema), '已复制 JSON Schema')"
                    >
                      复制定义
                    </el-button>
                  </div>
                  <pre class="schema-code">{{ formatSchema(tool.input_schema) }}</pre>
                </div>
              </el-collapse-item>
            </el-collapse>
          </div>
        </div>
      </div>

      <el-empty
        v-else-if="!loading"
        description="未发现可用工具，可点击上方「刷新工具」或进行探活"
        class="empty-drawer"
      />
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { McpServer, McpServerTool } from '@/types'
import { getMcpServerTools, refreshMcpServer } from '@/api/ai-mcp'

const visible = ref(false)
const loading = ref(false)
const refreshing = ref(false)
const server = ref<McpServer | null>(null)
const tools = ref<McpServerTool[]>([])
const filterQuery = ref('')

const filteredTools = computed(() => {
  if (!filterQuery.value.trim()) return tools.value
  const q = filterQuery.value.trim().toLowerCase()
  return tools.value.filter(
    t => t.tool_name.toLowerCase().includes(q) || (t.description || '').toLowerCase().includes(q)
  )
})

async function open(srv: McpServer) {
  server.value = srv
  filterQuery.value = ''
  visible.value = true
  await loadTools(srv.id)
}

async function loadTools(serverId: number) {
  loading.value = true
  try {
    tools.value = await getMcpServerTools(serverId)
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '读取工具列表失败')
  } finally {
    loading.value = false
  }
}

async function handleRefresh() {
  if (!server.value) return
  refreshing.value = true
  try {
    await refreshMcpServer(server.value.id)
    await loadTools(server.value.id)
    ElMessage.success('工具列表刷新成功')
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '刷新工具列表失败')
  } finally {
    refreshing.value = false
  }
}

function formatSchema(schema: unknown): string {
  if (!schema) return ''
  if (typeof schema === 'string') {
    try {
      return JSON.stringify(JSON.parse(schema), null, 2)
    } catch {
      return schema
    }
  }
  return JSON.stringify(schema, null, 2)
}

async function copyText(text: string, successMsg = '已复制到剪贴板') {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success(successMsg)
  } catch {
    ElMessage.error('复制失败，请手动选取复制')
  }
}

defineExpose({
  open,
})
</script>

<style scoped>
.tools-drawer-content {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.drawer-header-meta {
  padding: 12px 14px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
}

.meta-tag-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.command-box {
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--el-text-color-regular);
  word-break: break-all;
  background: var(--el-bg-color);
  padding: 6px 10px;
  border-radius: 6px;
  border: 1px solid var(--el-border-color-lighter);
}

.action-bar-top {
  display: flex;
  align-items: center;
  gap: 12px;
}

.filter-input {
  flex: 1;
}

.btn-icon {
  margin-right: 4px;
}

.tools-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  max-height: calc(100vh - 240px);
  overflow-y: auto;
  padding-right: 4px;
}

.tool-card {
  padding: 14px;
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  transition: all 0.2s;
}

.tool-card:hover {
  border-color: var(--el-color-primary-light-5);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.04);
}

.tool-card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 6px;
}

.tool-name {
  font-family: var(--font-mono, monospace);
  font-size: 14px;
  font-weight: 600;
  color: var(--el-color-primary);
  display: flex;
  align-items: center;
  gap: 6px;
}

.wrench-icon {
  font-size: 15px;
}

.copy-btn {
  font-size: 12px;
}

.tool-desc {
  font-size: 13px;
  color: var(--el-text-color-regular);
  line-height: 1.5;
  margin: 0 0 10px 0;
}

.schema-collapse-wrap :deep(.el-collapse) {
  border: none;
}

.schema-collapse-wrap :deep(.el-collapse-item__header) {
  height: 32px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
  border: none;
  background: transparent;
}

.schema-collapse-wrap :deep(.el-collapse-item__wrap) {
  border: none;
  background: transparent;
}

.schema-box {
  background: var(--el-fill-color-darker);
  border-radius: 6px;
  padding: 10px;
}

.schema-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 11px;
  color: var(--el-text-color-placeholder);
  margin-bottom: 6px;
}

.schema-code {
  margin: 0;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  line-height: 1.45;
  color: var(--el-text-color-primary);
  white-space: pre-wrap;
  word-break: break-all;
  max-height: 240px;
  overflow-y: auto;
}

.empty-drawer {
  margin-top: 40px;
}
</style>
