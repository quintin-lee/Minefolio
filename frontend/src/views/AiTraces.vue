<template>
  <div class="ai-traces-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>AI 对话追踪</h2>
      </div>
    </div>

    <div class="stats-bar" v-if="stats">
      <div class="stat-card">
        <div class="stat-value">{{ stats.total_traces ?? 0 }}</div>
        <div class="stat-label">总调用次数</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ formatMs(stats.avg_latency_ms) }}</div>
        <div class="stat-label">平均延迟</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ formatMs(stats.avg_first_token_ms) }}</div>
        <div class="stat-label">平均首 Token</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ formatTps(stats.avg_tokens_per_sec) }}</div>
        <div class="stat-label">平均 tok/s</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ formatTokens(stats.total_tokens) }}</div>
        <div class="stat-label">总 Token</div>
      </div>
    </div>

    <div class="filter-panel">
      <el-form :inline="true" :model="filters" class="premium-filters">
        <el-form-item label="供应商">
          <el-input v-model="filters.provider" placeholder="如 deepseek" clearable class="filter-select" />
        </el-form-item>
        <el-form-item label="模型">
          <el-input v-model="filters.model" placeholder="如 deepseek-chat" clearable class="filter-select" />
        </el-form-item>
        <el-form-item class="filter-actions">
          <el-button type="primary" class="search-btn" @click="loadData">查询</el-button>
          <el-button class="reset-btn" @click="resetFilters">重置</el-button>
        </el-form-item>
      </el-form>
    </div>

    <div class="table-container" v-loading="loading">
      <el-table :data="traces" class="premium-table" row-class-name="premium-row" header-cell-class-name="premium-header">
        <el-table-column prop="created_at" label="时间" width="170">
          <template #default="{ row }">
            <span class="mono-text">{{ formatDateTime(row.created_at) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="provider" label="供应商" width="100">
          <template #default="{ row }">
            <span>{{ row.provider || '—' }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="model" label="模型" min-width="140">
          <template #default="{ row }">
            <span class="mono-text">{{ row.model || '—' }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="total_tokens" label="Token" width="80" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ row.total_tokens ?? 0 }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="latency_ms" label="延迟" width="90" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ formatMs(row.latency_ms) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="first_token_ms" label="首 Token" width="90" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ formatMs(row.first_token_ms) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="tokens_per_sec" label="tok/s" width="80" align="right">
          <template #default="{ row }">
            <span class="mono-text">{{ formatTps(row.tokens_per_sec) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="status" label="状态" width="80" align="center">
          <template #default="{ row }">
            <el-tag :type="row.status === 'ok' ? 'success' : 'danger'" effect="light" round size="small">
              {{ row.status === 'ok' ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="80" align="center">
          <template #default="{ row }">
            <el-button type="primary" link @click="openDetail(row.id)">详情</el-button>
          </template>
        </el-table-column>
      </el-table>
      <div class="pagination-bar">
        <el-pagination v-model:current-page="page" v-model:page-size="pageSize" :total="total" :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next, jumper" background @current-change="loadData" @size-change="handleSizeChange" />
      </div>
    </div>

    <AiTraceDetail v-model="detailVisible" :trace-id="detailTraceId" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { listTraces, getTraceStats } from '@/api/ai-trace'
import type { AiTrace, AiTraceStats } from '@/api/ai-trace'
import AiTraceDetail from './AiTraceDetail.vue'

const traces = ref<AiTrace[]>([])
const stats = ref<AiTraceStats | null>(null)
const filters = ref({ provider: '', model: '' })
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)

const detailVisible = ref(false)
const detailTraceId = ref(0)

function formatDateTime(s: string) { return s ? s.replace('T', ' ').slice(0, 16) : '—' }
function formatMs(ms: number | string | undefined | null) {
  const n = Number(ms)
  return !isNaN(n) && n > 0 ? `${Math.round(n)}ms` : '—'
}
function formatTokens(t: number | string | undefined | null) {
  const n = Number(t)
  if (isNaN(n) || n === 0) return '0'
  return n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n)
}
function formatTps(tps: number | string | undefined | null) {
  const n = Number(tps)
  return !isNaN(n) && n > 0 ? n.toFixed(1) : '—'
}

async function loadData() {
  loading.value = true
  try {
    const params: Record<string, unknown> = { page: page.value, page_size: pageSize.value }
    if (filters.value.provider) params.provider = filters.value.provider
    if (filters.value.model) params.model = filters.value.model
    const raw = (await listTraces(params)) as unknown
    const res = (raw && typeof raw === 'object' && 'data' in raw && !Array.isArray((raw as { data: unknown }).data)
      ? (raw as { data: unknown }).data
      : raw) as { list?: AiTrace[]; total?: number } | AiTrace[]

    if (Array.isArray(res)) {
      traces.value = res
      total.value = res.length
    } else if (res && typeof res === 'object') {
      traces.value = Array.isArray(res.list) ? res.list : []
      total.value = typeof res.total === 'number' ? res.total : traces.value.length
    } else {
      traces.value = []
      total.value = 0
    }
  } catch (e) {
    console.error('[AiTraces] loadData failed:', e)
    traces.value = []
    total.value = 0
  } finally {
    loading.value = false
  }
}

function handleSizeChange() {
  page.value = 1
  loadData()
}

function resetFilters() {
  filters.value = { provider: '', model: '' }
  page.value = 1
  loadData()
}

function openDetail(id: number) {
  detailTraceId.value = id
  detailVisible.value = true
}

onMounted(async () => {
  try {
    const rawStats = (await getTraceStats()) as unknown
    const s = (rawStats && typeof rawStats === 'object' && 'data' in rawStats
      ? (rawStats as { data: unknown }).data
      : rawStats) as AiTraceStats
    stats.value = s || null
  } catch {
    // stats optional
  }
  loadData()
})
</script>

<style scoped>
.ai-traces-page {
  background-color: var(--mf-background);
  height: 100%;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.stats-bar {
  display: flex;
  gap: 16px;
}

.stat-card {
  flex: 1;
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  text-align: center;
}

.stat-value {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 20px;
  font-weight: 700;
  color: var(--mf-text-main);
}

.stat-label {
  font-size: 12px;
  color: var(--mf-text-muted);
  margin-top: 4px;
}

.filter-panel {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px 24px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.premium-filters {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  align-items: center;
}

.premium-filters :deep(.el-form-item) {
  margin-bottom: 0;
}

.filter-select {
  width: 170px;
}

.filter-actions {
  margin-left: auto;
}

.search-btn, .reset-btn {
  border-radius: var(--mf-radius-md);
}

.table-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 16px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  overflow: auto;
  display: flex;
  flex-direction: column;
  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table) {
  height: 100%;
  flex: 1;
  min-height: 0;
}

.table-container :deep(.el-table__body-wrapper) {
  overflow-y: auto;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-primary-light);
}

.pagination-bar {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}

:deep(.premium-header th) {
  background-color: var(--mf-primary-light) !important;
  color: var(--mf-text-muted) !important;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border) !important;
}

:deep(.premium-row) {
  transition: all 0.2s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border);
  padding: 16px 0;
  color: var(--mf-text-main);
}

:deep(.premium-row:hover > td) {
  background-color: var(--mf-primary-light) !important;
}

.mono-text {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  color: #64748b;
}
</style>
