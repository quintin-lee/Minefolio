<template>
  <div class="mcp-page">
    <!-- Header Banner -->
    <div class="page-header">
      <div class="header-intro">
        <h1 class="page-title">
          <Icon icon="ph:plugs-connected-duotone" class="title-icon" />
          MCP 工具箱 (Model Context Protocol)
        </h1>
        <p class="page-subtitle">
          连接与管理符合 Anthropic MCP 标准的外部数据与计算服务。模型在对话时将自动合并各启用的 MCP 工具并按需调用。
        </p>
      </div>

      <div class="header-actions">
        <el-button
          plain
          @click="$router.push('/skills')"
        >
          <Icon icon="ph:magic-wand-duotone" class="btn-icon" />
          Agent 技能库
        </el-button>

        <el-button
          plain
          :loading="pingingAll"
          @click="handlePingAll"
        >
          <Icon icon="ph:broadcast-duotone" class="btn-icon" />
          全部探活
        </el-button>

        <el-button
          type="primary"
          @click="openCreate"
        >
          <Icon icon="ph:plus-bold" class="btn-icon" />
          新建 MCP 服务
        </el-button>
      </div>
    </div>

    <!-- Filter & Stats Toolbar -->
    <div class="toolbar-card">
      <div class="search-filter-wrap">
        <el-input
          v-model="searchQuery"
          placeholder="搜索服务名称、命令或工具..."
          clearable
          class="search-input"
        >
          <template #prefix>
            <Icon icon="ph:magnifying-glass" class="search-icon" />
          </template>
        </el-input>

        <el-radio-group v-model="selectedTransport" size="default" class="transport-filter">
          <el-radio-button label="all">全部</el-radio-button>
          <el-radio-button label="stdio">Stdio 命令行</el-radio-button>
          <el-radio-button label="http">HTTP/SSE 远端</el-radio-button>
        </el-radio-group>
      </div>

      <div class="toggle-stats-wrap">
        <el-checkbox v-model="enabledOnly">
          仅显示已启用
        </el-checkbox>

        <div class="stats-pills">
          <span class="pill-item">
            共 <b>{{ servers.length }}</b> 个服务
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item success">
            已启用 <b>{{ enabledCount }}</b>
          </span>
          <span class="pill-divider">·</span>
          <span class="pill-item warning">
            可用工具 <b>{{ totalToolsCount }}</b> 个
          </span>
        </div>
      </div>
    </div>

    <!-- MCP Services Gallery -->
    <div v-loading="loading" class="mcp-grid-container">
      <div v-if="filteredServers.length > 0" class="mcp-grid">
        <McpCard
          v-for="server in filteredServers"
          :key="server.id"
          :server="server"
          :tools="serverToolsMap[server.id]"
          :testing="testingIds.has(server.id)"
          :refreshing="refreshingIds.has(server.id)"
          @edit="handleEdit"
          @delete="handleDelete"
          @toggle="handleToggle"
          @test="handleTest"
          @refresh="handleRefresh"
          @view-tools="handleViewTools"
        />
      </div>

      <el-empty
        v-else-if="!loading"
        description="未检索到匹配的 MCP 服务"
        class="empty-state"
      >
        <template #extra>
          <el-button type="primary" plain @click="openCreate">新建第一个 MCP 服务</el-button>
        </template>
      </el-empty>
    </div>

    <!-- Dialogs & Drawer -->
    <McpDialog ref="dialogRef" @saved="loadServers" />
    <McpToolsDrawer ref="toolsDrawerRef" />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Icon } from '@iconify/vue'
import type { McpServer, McpServerTool } from '@/types'
import { listMcpServers, getMcpServerTools, deleteMcpServer, testMcpServer, refreshMcpServer } from '@/api/ai-mcp'
import McpCard from '@/components/mcp/McpCard.vue'
import McpDialog from '@/components/mcp/McpDialog.vue'
import McpToolsDrawer from '@/components/mcp/McpToolsDrawer.vue'

const loading = ref(false)
const pingingAll = ref(false)
const servers = ref<McpServer[]>([])
const serverToolsMap = reactive<Record<number, McpServerTool[]>>({})
const testingIds = reactive(new Set<number>())
const refreshingIds = reactive(new Set<number>())

const searchQuery = ref('')
const selectedTransport = ref('all')
const enabledOnly = ref(false)

const dialogRef = ref<InstanceType<typeof McpDialog>>()
const toolsDrawerRef = ref<InstanceType<typeof McpToolsDrawer>>()

const enabledCount = computed(() => {
  return servers.value.filter(s => Number(s.enabled) === 1).length
})

const totalToolsCount = computed(() => {
  return servers.value.reduce((sum, s) => sum + (s.tool_count || 0), 0)
})

const filteredServers = computed(() => {
  return servers.value.filter(s => {
    if (enabledOnly.value && Number(s.enabled) !== 1) {
      return false
    }
    if (selectedTransport.value !== 'all' && s.transport !== selectedTransport.value) {
      return false
    }
    if (searchQuery.value.trim()) {
      const q = searchQuery.value.trim().toLowerCase()
      const matchName = s.name.toLowerCase().includes(q)
      const matchTarget = (s.command || s.url || '').toLowerCase().includes(q)
      const tools = serverToolsMap[s.id] || []
      const matchTools = tools.some(t => t.tool_name.toLowerCase().includes(q) || (t.description || '').toLowerCase().includes(q))
      if (!matchName && !matchTarget && !matchTools) {
        return false
      }
    }
    return true
  })
})

async function loadServers() {
  try {
    loading.value = true
    const list = await listMcpServers()
    servers.value = list.map(s => ({
      ...s,
      id: Number(s.id),
      enabled: Number(s.enabled) === 1 ? 1 : 0,
    }))
    // Load tools previews in background
    for (const s of servers.value) {
      getMcpServerTools(s.id).then(tools => {
        serverToolsMap[s.id] = tools
      }).catch(() => {
        // Silent catch for background tools pre-fetch
      })
    }
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '加载 MCP 服务列表失败')
  } finally {
    loading.value = false
  }
}

function openCreate() {
  dialogRef.value?.openCreate()
}

function handleEdit(server: McpServer) {
  dialogRef.value?.openEdit(server)
}

function handleViewTools(server: McpServer) {
  toolsDrawerRef.value?.open(server)
}

function handleToggle(server: McpServer, enabled: boolean) {
  const target = servers.value.find(s => s.id === server.id)
  if (target) {
    target.enabled = enabled ? 1 : 0
  }
}

async function handleTest(server: McpServer) {
  testingIds.add(server.id)
  try {
    const res = await testMcpServer(server.id)
    if (res.success || res.status === 'ok') {
      ElMessage.success(`探活成功: 发现 ${res.tools?.length || 0} 个可用工具`)
      await loadServers()
    } else {
      ElMessage.warning(res.message || '探活返回异常')
    }
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '探活测试失败')
  } finally {
    testingIds.delete(server.id)
  }
}

async function handleRefresh(server: McpServer) {
  refreshingIds.add(server.id)
  try {
    const res = await refreshMcpServer(server.id)
    ElMessage.success(`刷新成功: 当前已缓存 ${res.tool_count || 0} 个工具`)
    await loadServers()
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '刷新工具列表失败')
  } finally {
    refreshingIds.delete(server.id)
  }
}

async function handlePingAll() {
  if (servers.value.length === 0) {
    ElMessage.info('暂无已配置的 MCP 服务')
    return
  }
  pingingAll.value = true
  let successCount = 0
  try {
    for (const s of servers.value) {
      try {
        const res = await testMcpServer(s.id)
        if (res.success || res.status === 'ok') successCount++
      } catch {
        // continue pinging
      }
    }
    ElMessage.success(`全量探活完成：${successCount}/${servers.value.length} 个服务正常在线`)
    await loadServers()
  } finally {
    pingingAll.value = false
  }
}

async function handleDelete(server: McpServer) {
  try {
    await ElMessageBox.confirm(
      `确定要删除 MCP 服务「${server.name}」吗？删除后其提供的全部工具将无法在 AI 对话中被调用。`,
      '确认删除 MCP 服务',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )
    await deleteMcpServer(server.id)
    ElMessage.success('MCP 服务已成功删除')
    await loadServers()
  } catch {
    // User cancelled
  }
}

onMounted(() => {
  loadServers()
})
</script>

<style scoped>
.mcp-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
  max-width: 1400px;
  margin: 0 auto;
  padding-bottom: 32px;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 16px;
  padding: 24px;
  background: var(--el-bg-color);
  border-radius: 14px;
  border: 1px solid var(--el-border-color-light);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04);
}

.header-intro {
  max-width: 720px;
}

.page-title {
  margin: 0 0 6px 0;
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  display: flex;
  align-items: center;
  gap: 10px;
  letter-spacing: -0.02em;
}

.title-icon {
  font-size: 26px;
  color: var(--el-color-primary);
}

.page-subtitle {
  margin: 0;
  font-size: 13.5px;
  line-height: 1.55;
  color: var(--el-text-color-secondary);
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.btn-icon {
  font-size: 15px;
  margin-right: 5px;
}

.toolbar-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 16px 20px;
  background: var(--el-bg-color);
  border-radius: 12px;
  border: 1px solid var(--el-border-color-light);
}

.search-filter-wrap {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 14px;
}

.search-input {
  width: 320px;
}

.search-icon {
  font-size: 16px;
  color: var(--el-text-color-placeholder);
}

.transport-filter :deep(.el-radio-button__inner) {
  font-size: 12.5px;
}

.toggle-stats-wrap {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  padding-top: 4px;
  border-top: 1px solid var(--el-border-color-lighter);
}

.stats-pills {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: var(--el-text-color-secondary);
}

.pill-item b {
  color: var(--el-text-color-primary);
}

.pill-item.success b {
  color: var(--el-color-success);
}

.pill-item.warning b {
  color: var(--el-color-warning);
}

.pill-divider {
  color: var(--el-border-color);
}

.mcp-grid-container {
  min-height: 280px;
}

.mcp-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
  gap: 16px;
}

.empty-state {
  padding: 60px 0;
  background: var(--el-bg-color);
  border-radius: 12px;
  border: 1px dashed var(--el-border-color);
}

@media (max-width: 768px) {
  .page-header {
    flex-direction: column;
    align-items: flex-start;
  }
  .header-actions {
    width: 100%;
    justify-content: flex-start;
    flex-wrap: wrap;
  }
  .search-input {
    width: 100%;
  }
  .mcp-grid {
    grid-template-columns: 1fr;
  }
}
</style>
